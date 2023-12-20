struct file {
#ifdef LAB_NET
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_SOCK } type;
#else
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
#endif
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
#ifdef LAB_NET
  struct sock *sock; // FD_SOCK
#endif
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode 的内存副本
struct inode {
  // 设备号，表示inode所属的设备。
  uint dev;           
  uint inum; // inode号，表示inode在设备上的唯一标识符。
  // 引用计数，表示有多少个指向这个inode的指针。当引用计数为0时，inode可以被释放。
  int ref;            
  struct sleeplock lock; // 保护该inode结构的睡眠锁
  // 是否已经从磁盘上读取到内存中。当inode首次被读取时，此字段会被设置为1。
  int valid; 
  short type; // 文件类型。可能的取值包括T_FILE（普通文件）、T_DIR（目录）、T_DEV（设备文件）等。
  short major; // 当文件类型是设备文件（T_DEV）时，表示设备的主设备号
  short minor; // 当文件类型是设备文件（T_DEV）时，表示设备的次设备号。
  short nlink; // 文件的硬链接数，表示有多少个目录项指向该inode。
  uint size; // 文件大小，表示文件的字节大小。
  // 存储文件数据块的数组。前NDIRECT个元素存储直接块的地址，最后一个元素存储间接块的地址。
  // 每个块的大小为BSIZE（磁盘块大小）。
  uint addrs[NDIRECT+1];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
#define STATS   2
