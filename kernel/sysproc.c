#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

// 系统调用 sys_sbrk 的实现
// 返回值:
//   -1: 参数错误或操作失败
//   其他: 新的堆顶地址
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p;

  // 从用户态获取参数 n
  if(argint(0, &n) < 0)
    return -1;

  // 获取当前进程
  p = myproc();

  // 获取当前堆顶地址
  addr = p->sz;

  // lab5-1
  // 增加堆的大小但不分配内存
  if(n >= 0 && addr + n >= addr){
    p->sz += n;
  } else if(n < 0 && addr + n >= PGROUNDUP(p->trapframe->sp)){
    // 处理负数的 n，并确保 addr 在用户栈之上 - lab5-3
    // 调用 uvmdealloc 函数释放内存
    p->sz = uvmdealloc(p->pagetable, addr, addr + n);
  } else {
    return -1; // 参数错误
  }

  // 返回新的堆顶地址
  return addr;
}


uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
