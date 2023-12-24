#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

int
mmap_lazyalloc(pagetable_t pagetable, uint64 va)
{ 
  struct proc *p = myproc();  // 获取当前进程
  struct file *f;             // 映射文件的文件结构
  int prot;                   // 映射的保护标志
  int has_a_vam = 0;          // 检查虚拟地址是否在任何VMA范围内的标志
  int perm = 0;               // 映射的权限
  char *mem;                  // 指向分配的物理内存的指针
  // 查找包含虚拟地址va的VMA（虚拟内存区域）
  int i;
  for(i = 0; i < 16; i++){
    if(p->vmas[i].addr <= va && va < (p->vmas[i].addr + p->vmas[i].length)){
      has_a_vam = 1;
      f = p->vmas[i].f;
      prot = p->vmas[i].prot;
      break;
    }
  }
  // 如果虚拟地址不在任何VMA范围内，则返回-1
  if(has_a_vam == 0){
    return -1;
  }
  // 设置PTE_U以允许用户模式访问
  perm |= PTE_U;
  // 设置PTE_R、PTE_W、PTE_X权限标志
  if(prot & PROT_READ){
    perm |= PTE_R;
  }
  if(prot & PROT_WRITE){
    perm |= PTE_W;
  }  
  if(prot & PROT_EXEC){
    perm |= PTE_X;
  }
  // 分配物理内存，注意这里存在一个大bug，没有为所有虚拟地址分配mem(4096)的内存
  if((mem = kalloc()) == 0){
    return -1;
  }
  // 在mmaptest/makefile()中，创建一个要映射的文件，其中包含1.5页的'A'和半页的零。
  // 因此，在获取mem之后，我们必须将长度的0设置为mem
  memset(mem, 0, PGSIZE);

  // 将物理内存映射到虚拟地址
  if(mappages(pagetable, va, PGSIZE, (uint64)mem, perm) == -1){
    kfree(mem);
    return -1;
  }
  // 没有设置PTE_D，因为总是直接将数据写回文件中，在munmap()中进行处理
  // 读取文件数据，然后将数据放入va
  ilock(f->ip);
  if(readi(f->ip, 1, va, va - p->vmas[i].addr, PGSIZE) < 0){ // readi中的偏移量为 'va - p->vmas[i].addr'  
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);
  p->vmas[i].offset += PGSIZE;
  // 成功，返回0
  return 0;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();

    // Fill in the page table lazily, in response to page faults. 
  } else if(r_scause() == 13){
    uint64 fault_va = r_stval();
    int is_alloc = mmap_lazyalloc(p->pagetable, fault_va);
    if(fault_va > p->sz || is_alloc == -1){
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    // printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    // printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

