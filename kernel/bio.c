// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 7

// 创建哈希表，锁，和hash函数
struct {
  struct spinlock lock[NBUCKET]; // 每个桶都有一个锁用于同步
  struct buf buf[NBUF]; // 实际的缓冲区
  struct buf head[NBUCKET];
} bcache; // 包含了缓冲区哈希表，使用了哈希桶的方式，共有 NBUCKET 个桶

int hash (int n) {
  return n % NBUCKET;
}

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];
//   // Linked list of all buffers, through prev/next.
//   // head.next is most recently used.
//   struct buf head;
// } bcache;

//辅助函数，用于写cache
void 
write_cache(struct buf *take_buf, uint dev, uint blockno)
{
  take_buf->dev = dev;
  take_buf->blockno = blockno;
  take_buf->valid = 0;
  take_buf->refcnt = 1;
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

//跟上一实验一样的做法，初始化锁并将所有块挂在第一个池子里
void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&(bcache.lock[i]), "bcache.hash");
  }

  bcache.head[0].next = &bcache.buf[0];
  for (b = bcache.buf; b < bcache.buf+NBUF-1; b++) {
    b->next = b+1;
    initsleeplock(&b->lock, "buffer");
  }
  initsleeplock(&b->lock, "buffer");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  // Is the block already cached?
  struct buf *b;
  struct buf *last_b;
  struct buf *take_buf = 0;
  int id = hash(blockno);
  acquire(&bcache.lock[id]);

  for(b = bcache.head[id].next, last_b = &(bcache.head[id]); b; b = b->next, last_b = last_b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
    if(b->refcnt == 0)
    {
      take_buf = b;
    }
  }

  if(take_buf)
  {
    write_cache(take_buf, dev, blockno);
    release(&bcache.lock[id]);
    acquiresleep(&take_buf->lock);
    return take_buf;
  }

  int lock_num = -1;
  uint time = __UINT32_MAX__;
  struct buf *last_take = 0;
  struct buf *tmp;


  for(int i = 1; i <= NBUCKET/2; ++i)
  {
    int j = id - i >=0 ? id - i : id + (NBUCKET - i);
    // int j = (id + i)%NBUCKET;

    acquire(&bcache.lock[j]);


    for(b = bcache.head[j].next, tmp = &(bcache.head[j]); b; b = b->next, tmp = tmp->next)
    {
      if (b->refcnt == 0) 
      {
        if (b->time < time) 
        {
          time = b->time;
          last_take = tmp;
          take_buf = b;

          if (lock_num != -1 && lock_num != j && holding(&bcache.lock[lock_num])) 
            release(&bcache.lock[lock_num]);

          lock_num = j;
        }   
      }
    }

    if (j!=id && j!=lock_num && holding(&bcache.lock[j])) 
      release(&bcache.lock[j]);
  }

  if (!take_buf) 
    panic("bget: no buffers");

  last_take->next = take_buf->next;
  take_buf->next = 0;
  release(&bcache.lock[lock_num]);


  last_b->next = take_buf;
  take_buf->next = 0;
  write_cache(take_buf, dev, blockno);


  release(&bcache.lock[id]);
  acquiresleep(&take_buf->lock);

  return take_buf;


}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
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