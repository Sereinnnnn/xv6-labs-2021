#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 7

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];
//   // Linked list of all buffers, through prev/next.
//   // head.next is most recently used.
//   struct buf head;
// } bcache;

// 创建哈希表，锁，和hash函数
struct {
  struct spinlock lock[NBUCKET]; // 每个桶都有一个锁用于同步
  // 实际的缓冲区；这些缓冲区会根据哈希函数的计算结果被放入相应的桶中。
  struct buf buf[NBUF]; 
  struct buf head[NBUCKET];
} bcache; // 包含了缓冲区哈希表，使用了哈希桶的方式，共有 NBUCKET 个桶

/*
  哈希桶是一种数据结构，用于实现哈希表。
  在这个上下文中，哈希桶是指由锁保护的缓冲区链表数组。
  每个哈希桶包含一组缓冲区，通过哈希函数计算得到。
  哈希函数的目的是将块号映射到特定的哈希桶，以便更有效地查找和操作缓冲区。

  哈希桶是哈希表中的一个槽位或存储位置，用于存储数据元素。
  哈希桶可以是一个数组元素，也可以是一个链表头节点，用于处理哈希冲突时的链式存储。
  因为通过在哈希表的每个槽位（桶）中使用链表或其他数据结构，可以在相同哈希值的桶内存储多个数据元素，从而解决冲突。

  哈希桶的使用使得哈希表的实现变得更加简单。
  在每个桶中使用链表等结构来存储数据，可以方便地处理冲突，不需要在整个哈希表中进行复杂的数据移动或重排。
*/

// 使用哈希函数计算块号对应的哈希桶
int hash (int n) {
  return n % NBUCKET;
}

//辅助函数，用于写cache
void 
write_cache(struct buf *take_buf, uint dev, uint blockno)
{
  // 设置缓存块的设备号
  take_buf->dev = dev;
  
  // 设置缓存块的块号
  take_buf->blockno = blockno;
  
  // 将缓存块标记为无效，表示需要重新读取数据。
  /*
    因为缓存块即将被用于存储新的数据。通过将 valid 设置为0，标志着该缓存块的内容已经过期，需要在下次使用前重新从存储设备读取最新的数据。这是文件系统中一种常见的策略，确保缓存中的数据与实际存储设备上的数据保持同步。在需要访问特定块的数据时，系统首先检查缓存中是否已经有了这个块的副本，如果有，就可以直接使用。但是，如果 valid 标志为0，系统会重新从设备中读取数据，确保缓存中的内容是最新的。
  */
  take_buf->valid = 0;
  
  // 将缓存块的引用计数设置为1，表示有一个引用指向该缓存块
  take_buf->refcnt = 1;
  
  // 更新缓存块的时间戳，使用当前系统时钟滴答数（ticks）
  take_buf->time = ticks;
}

// void
// binit(void)
// {
//   struct buf *b;
//   initlock(&bcache.lock, "bcache");
//   // Create linked list of buffers
//   bcache.head.prev = &bcache.head;
//   bcache.head.next = &bcache.head;
//   for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     initsleeplock(&b->lock, "buffer");
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
// }

// 跟上一实验一样的做法，初始化锁并将所有块挂在第一个池子里
// 初始化缓冲区
/*
  最终，整个缓冲区就以哈希桶的形式组织，每个桶都有一个锁用于同步，并且所有缓存块通过链表连接在第一个哈希桶上。
  这样的布局有助于提高缓冲区的查找效率和并发访问的性能。
*/
void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&(bcache.lock[i]), "bcache.hash");
  }
  // 将第一个哈希桶的头指针指向第一个缓存块
  bcache.head[0].next = &bcache.buf[0];
  // 循环遍历所有的缓存块，将它们连接成一个链表
  for (b = bcache.buf; b < bcache.buf+NBUF-1; b++) {
    // 当前缓存块的 next 指针指向下一个缓存块
    b->next = b+1;
    // 对每个缓存块的锁进行初始化
    initsleeplock(&b->lock, "buffer");
  }
  initsleeplock(&b->lock, "buffer");
}

// 查看缓冲区缓存以查找设备开发上的块。
// 如果没有找到，则分配一个缓冲区。
// 无论哪种情况，都返回锁定的缓冲区。
// w：分配内存区域，类似 malloc
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b; // 迭代访问缓冲区链表的指针，是当前遍历到的缓存块
  struct buf *last_b; // 记录上一个缓冲区的指针，以便后续插入操作
  struct buf *take_buf = 0; // 记录将要被替换的缓冲区
  int id = hash(blockno); // 用哈希函数计算出块号对应的哈希桶
  acquire(&bcache.lock[id]); // 获取对当前哈希桶的锁，确保在缓冲区操作期间不会被其他线程中断。

  // 从哈希桶的头节点开始遍历缓冲区链表
  for(b = bcache.head[id].next, last_b = &(bcache.head[id]); b; b = b->next, last_b = last_b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 增加引用计数
      release(&bcache.lock[id]); // 释放哈希桶的锁
      acquiresleep(&b->lock); // 获取缓存块的锁
      return b;
    }
    if(b->refcnt == 0)
    {
      take_buf = b;
    }
  }

  if(take_buf) // 存在引用计数为零的缓冲区块 take_buf
  {
    write_cache(take_buf, dev, blockno); // 则调用 write_cache 将其初始化
    release(&bcache.lock[id]); // 释放哈希桶的锁
    acquiresleep(&take_buf->lock); // 获取缓存块的锁
    return take_buf;
  }

  // 从哈希桶中选择一个空闲的缓冲区块（b->refcnt == 0）以便后续使用
  int lock_num = -1;
  uint time = __UINT32_MAX__;
  struct buf *last_take = 0;
  struct buf *tmp;


  for(int i = 1; i <= NBUCKET/2; ++i)
  {
    int j = id - i >=0 ? id - i : id + (NBUCKET - i); // 当前要访问的哈希桶的索引
    // int j = (id + i)%NBUCKET;

    acquire(&bcache.lock[j]);

    // 在当前哈希桶中的缓冲区链表上进行遍历，寻找引用计数为零的缓冲区块
    for(b = bcache.head[j].next, tmp = &(bcache.head[j]); b; b = b->next, tmp = tmp->next)
    {
      if (b->refcnt == 0) // 如果找到引用计数为零的缓冲区块，记录相关信息
      {
        if (b->time < time) 
        {
          time = b->time;
          last_take = tmp;
          take_buf = b;

          // 如果之前已经锁住了其他哈希桶，释放之前的锁
          if (lock_num != -1 && lock_num != j && holding(&bcache.lock[lock_num])) 
            release(&bcache.lock[lock_num]);

          // 记录当前锁住的哈希桶索引
          lock_num = j;
        }   
      }
    }

    // 如果不是当前哈希桶，并且之前锁住了其他哈希桶，释放之前的锁
    if (j!=id && j!=lock_num && holding(&bcache.lock[j])) 
      release(&bcache.lock[j]);
  }

  if (!take_buf) 
    panic("bget: no buffers");

  last_take->next = take_buf->next; // 从原哈希桶中移除
  take_buf->next = 0;
  release(&bcache.lock[lock_num]);

  last_b->next = take_buf; // 将 take_buf 插入到新的哈希桶中
  take_buf->next = 0;
  write_cache(take_buf, dev, blockno); // 初始化并释放锁

  release(&bcache.lock[id]);
  acquiresleep(&take_buf->lock);

  return take_buf;
}

// 返回一个锁定的缓冲区（struct buf），该缓冲区包含指定块的内容。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  // 获取具有指定设备和块号的缓冲区（如果缓冲区不在内存中，则将其读取到内存中）
  b = bget(dev, blockno);
  // 如果缓冲区的内容无效（未被读取过），则通过virtio_disk_rw函数将其内容从磁盘读取到缓冲区中
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
// void
// brelse(struct buf *b)
// {
//   if(!holdingsleep(&b->lock))
//     panic("brelse");
//   releasesleep(&b->lock);
//   acquire(&bcache.lock);
//   b->refcnt--;
//   if (b->refcnt == 0) {
//     // no one is waiting for it.
//     b->next->prev = b->prev;
//     b->prev->next = b->next;
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
//   release(&bcache.lock);
// }

//获取锁，然后将块引用计数-1，并不明白提示所说的不用锁的方法是什么
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;  
  release(&bcache.lock[id]);
}

// void
// bpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt++;
//   release(&bcache.lock);
// }

//获取锁，引用计数+1
void
bpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&(bcache.lock[id]));
  b->refcnt++;
  release(&(bcache.lock[id]));
}

// void
// bunpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt--;
//   release(&bcache.lock);
// }

//获取锁，引用计数-1
void
bunpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&(bcache.lock[id]));
  b->refcnt--;
  release(&(bcache.lock[id]));
}