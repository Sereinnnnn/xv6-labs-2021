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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  printf("PHYSTOP is %p\n", PHYSTOP);
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
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// 分配一个物理内存页面，大小为4096字节（PGSIZE）。
// 返回内核可以使用的指针。
// 如果无法分配内存，则返回0。
void *
kalloc(void)
{
  struct run *r; // xv6内核中用于管理空闲内存页面的数据结构。

  // 获取内存管理器锁
  acquire(&kmem.lock);

  // 从空闲列表中获取一个空闲内存页面
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;

  // 释放内存管理器锁
  release(&kmem.lock);

  // 将分配的内存填充为垃圾数据
  if(r)
    memset((char*)r, 5, PGSIZE);

  // 返回分配的内存页面的指针
  return (void*)r;
}

// used for tracing purposes in exp2
// void *kget_freelist(void) { return kmem.freelist; } 