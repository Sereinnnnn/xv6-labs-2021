// 磁盘上的文件系统形式
// Both the kernel and user programs use this header file.

#define ROOTINO  1  // 根目录的inode号码
#define BSIZE 1024  // 块的大小，每个块包含固定数量的字节

// xv6（文件系统）将磁盘划分为几个部分
// [ boot block | super block | log | inode blocks | free bit map | data blocks]

// 描述整个文件系统布局的结构
struct superblock {
  uint magic;        // 文件系统的标识，必须是预定义的常量 FSMAGIC
  uint size;         // 文件系统镜像的大小（以块为单位）
  uint nblocks;      // 数据块的数量
  uint ninodes;      // inode的数量
  uint nlog;         // 日志块的数量
  uint logstart;     // 第一个日志块的块号
  uint inodestart;   // 第一个inode块的块号
  uint bmapstart;    // 第一个空闲位图块的块号
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// 文件系统中磁盘上的索引节点（inode）结构，每个inode对应一个文件或目录。
/*
  这个结构定义了文件在磁盘上的物理表示方式，
  而在内存中有一个相应的结构（上面提到的 struct inode）来在操作系统中进行处理和管理。
*/
struct dinode {
  short type;  //  文件类型，可能包括 T_FILE（普通文件）、T_DIR（目录）和T_DEVICE（设备文件）等。
  short major; // 主设备号 (T_DEVICE only，即仅在文件类型为设备文件（T_DEVICE）时有意义)
  short minor; // 次设备号
  // 链接数，表示有多少目录项指向这个inode。
  // 每当创建一个新的硬链接时，该值会增加。当值降为零时，表示没有目录项指向该inode，可以释放。
  short nlink; 
  // 文件的大小，以字节为单位。对于目录，这通常是目录项的总大小。
  uint size;   
  /*
    数据块地址数组，存储文件实际的数据块地址。
    数组的大小为 NDIRECT + 1，其中 NDIRECT 是直接数据块的数量。
    如果文件的大小小于或等于 NDIRECT * BSIZE（块大小），则所有数据块都存储在 addrs 数组中。
    否则，还会使用一级间接块。
  */
  uint addrs[NDIRECT+1]; // Data block addresses
};

// 每个块包含的inode数
#define IPB           (BSIZE / sizeof(struct dinode))

// 给定inode号和超级块，返回包含该inode的块号
// 用于将给定的inode号转换为文件系统中相应的块号，以便在磁盘上找到包含该inode信息的块。
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// 每个块包含的位图位数
#define BPB           (BSIZE*8)

// 给定块号和超级块，返回包含该块的位图块号
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// 目录项的最大名称长度
#define DIRSIZ 14

// 目录中的文件项结构，包含inode号和文件名
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

