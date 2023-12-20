// struct buf {
//   int valid;   // has data been read from disk?
//   int disk;    // does disk "own" buf?
//   uint dev;
//   uint blockno;
//   struct sleeplock lock;
//   uint refcnt;
//   struct buf *prev; // LRU cache list
//   struct buf *next;
//   uchar data[BSIZE];
// };

struct buf {
  int valid;   // 是否包含磁盘块的有效数据
  int disk;    // does disk "own" buf?
  uint dev;    // 磁盘设备的设备号
  uint blockno; // 磁盘块号
  struct sleeplock lock; // 用于同步的睡眠锁
  uint refcnt; // 缓冲区的引用计数
  struct buf *prev; // LRU cache list
  struct buf *next; // 用于构建链表的指针
  uchar data[BSIZE];
  // 为cache块打上时间戳，每次使用时更新，寻找空闲块时，优先选择最久未被使用的空闲块。
  uint time; // 最后一次被使用的时间
};