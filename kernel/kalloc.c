// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
// 实现了一个物理内存分配器，主要用于分配给用户进程、内核栈、页表页和管道缓冲区等，每次分配都是整个4096字节页面，即一个页面。
// 保持粒度的内存分配通常更高效和简单：对齐、简化管理、性能考虑、内存映射（内存以页面为单位进行映射）、硬件支持（大多数计算机体系结构和操作系统都以页面为基本单位进行内存管理。硬件的内存管理单元（MMU）通常以页面为单位进行地址转换和访问控制）

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// 一个物理内存块
// 这个结构定义了一个简单的链表节点，链表的头部是 kmem.freelist 中的第一个节点。
struct run {
  struct run *next;
};

// 内存分配器结构
// 定义了一个带锁的结构，包含一个用于锁住内存分配器的锁，以及一个指向空闲内存块的指针。用于管理内核的物理内存分配。
struct {
  struct spinlock lock; // 这是一个自旋锁，用于确保在多个内核线程之间对内存分配的操作是原子的。这是因为内存分配是一个共享资源，需要在多线程环境中进行同步。
  struct run *freelist; // 这是一个指向空闲内存块链表的指针。freelist 维护了一个链表，其中的每个节点都代表一个可用的物理内存块。
} kmem;

// 初始化内核内存分配器
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP); // 调用 freerange 函数将内核代码和数据段之后的空闲物理内存加入到空闲列表。
}

// 释放指定范围的内存页面，并加入到空闲列表中。
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
// 释放一个物理内存页面，将其加入到空闲列表中。
void
kfree(void *pa)
{
  struct run *r;

  // 确保物理地址合法，且不在内核代码段和数据段范围内
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 用特定值填充页面，用于捕捉悬空引用
  memset(pa, 1, PGSIZE);

  // 将释放的页面加入到空闲列表中
  r = (struct run*)pa;

  // 获取内核内存锁，确保对内存的释放是原子操作
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// (在内核中)分配物理内存页面，从空闲列表中取出一个节点并返回其指针。
void *
kalloc(void)
{
  struct run *r;

  // 通过获取内核内存锁，确保对内核内存的分配是原子操作
  acquire(&kmem.lock);

  // 从空闲列表中获取一个节点
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;  // 将空闲列表的指针指向下一个节点

  // 释放内核内存锁
  release(&kmem.lock);

  // 如果成功获取了节点，用 5 填充页面（填充为 5，可能是为了标记已分配的内存）
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  // 返回分配的页面的指针
  return (void*)r;
}
