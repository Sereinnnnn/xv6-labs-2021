// Mutual exclusion spin locks.
// 实现了自旋锁（spin lock）的基本功能，确保对临界区的互斥访问

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

// 初始化自旋锁
void
initlock(struct spinlock *lk, char *name) // name：表示锁的名称的字符串
{
  lk->name = name;
  lk->locked = 0; // 未被锁定
  lk->cpu = 0; // 没有 CPU 持有这个锁
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.首先禁用中断（通过 push_off 函数），以避免死锁。
  if(holding(lk)) // 检查当前 CPU 是否已经持有锁，如果是则触发 panic
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0) // 用 __sync_lock_test_and_set 函数来尝试原子地获取锁，如果锁已经被其他 CPU 持有，就一直自旋等待。
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize(); // 获取锁后，使用 __sync_synchronize 确保在进入临界区之前的所有内存访问都已完成。

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu(); // 记录当前 CPU 持有锁
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk)) // 先检查当前 CPU 是否确实持有锁，如果不是则触发 panic
    panic("release");

  lk->cpu = 0; // 清除 lk->cpu 表示当前 CPU 不再持有锁

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize(); // 确保在离开临界区之前的所有内存访问都已完成

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked); // 原子地释放锁。

  pop_off(); // 还原中断状态
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
// 返回值为非零表示当前 CPU 持有锁。
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
// 禁用中断，并记录中断状态以备后续还原。
void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

// 检查中断是否处于禁用状态，以及调用的次数是否匹配。如果是，则还原中断状态。
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
