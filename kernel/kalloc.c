// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

// 首先是多内存池创建和锁管理
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 将kmem修改为数组，这样每个cpu对应一份freelist和lock; 用cpuid来分配内存池

// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }

// 初始化kmem时将每个cpu对应kmem[i]都初始化
void
kinit()
{
  for(int i=0;i<NCPU;i++)
  {
      initlock(&kmem[i].lock, "kmem"); // 初始化所有锁
  }
  freerange(end, (void*)PHYSTOP); // 默认将(所有)内存分配给运行这一函数的cpu
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void
// kfree(void *pa)
// {
//   struct run *r;

//   if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(pa, 1, PGSIZE);

//   r = (struct run*)pa;

//   acquire(&kmem.lock);
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   release(&kmem.lock);
// }

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// void *
// kalloc(void)
// {
//   struct run *r;

//   acquire(&kmem.lock);
//   r = kmem.freelist;
//   if(r)
//     kmem.freelist = r->next;
//   release(&kmem.lock);

//   if(r)
//     memset((char*)r, 5, PGSIZE); // fill with junk
//   return (void*)r;
// }

// kfree本来是将内存块释放到单一内存块中，修改是需要放回该cpu对应的内存池
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  push_off();// turn interrupts off
  int i=cpuid();// core number
  acquire(&kmem[i].lock); //获取锁以保证内存池的使用安全
  r->next = kmem[i].freelist;
  kmem[i].freelist = r;
  release(&kmem[i].lock);
  
  pop_off();//turn inturrupts on
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 内存块获取
void *
kalloc(void)
{
  struct run *r;

  push_off();// turn interrupts off
  int i=cpuid();// core number
  acquire(&kmem[i].lock);
  r = kmem[i].freelist;
  // 先从自己的内存池中寻找可用的内存块
  if(r)
  {
    kmem[i].freelist = r->next; 
  }
  release(&kmem[i].lock);

  if(!r) // 当前cpu对应freelist为空
  {
     for(int j=0;j<NCPU;j++) // 从其他cpu的freelist中借用
     {
       if(j!=i)
       {
          acquire(&kmem[j].lock); // 寻找前记得上锁
          if(kmem[j].freelist)
	  {
	      r=kmem[j].freelist;
              kmem[j].freelist=r->next; 	      
	      release(&kmem[j].lock);
	      break;
	  }
          release(&kmem[j].lock);
       }
     }
  }
  
  pop_off();//turn on inturrupt

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}