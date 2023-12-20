#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "kernel/types.h"
#include "user/user.h"

void test0();
void test1();

#define SZ 4096
char buf[SZ];

int
main(int argc, char *argv[])
{
  // test0();
  test1();
  exit(0);
}

void
createfile(char *file, int nblock)
{
  int fd;
  char buf[BSIZE];
  int i;
  
  fd = open(file, O_CREATE | O_RDWR);
  if(fd < 0){
    printf("createfile %s failed\n", file);
    exit(-1);
  }
  for(i = 0; i < nblock; i++) {
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("write %s failed\n", file);
      exit(-1);
    }
  }
  close(fd);
}

void
readfile(char *file, int nbytes, int inc)
{
  char buf[BSIZE];
  int fd;
  int i;

  if(inc > BSIZE) {
    printf("readfile: inc too large\n");
    exit(-1);
  }
  if ((fd = open(file, O_RDONLY)) < 0) {
    printf("readfile open %s failed\n", file);
    exit(-1);
  }
  for (i = 0; i < nbytes; i += inc) {
    if(read(fd, buf, inc) != inc) {
      printf("read %s failed for block %d (%d)\n", file, i, nbytes);
      exit(-1);
    }
  }
  close(fd);
}

int ntas(int print)
{
  int n;
  char *c;

  // 使用 statistics 函数获取文件系统统计信息，将结果存储在 buf 缓冲区中
  if (statistics(buf, SZ) <= 0) {
    fprintf(2, "ntas: no stats\n");
  }
  c = strchr(buf, '=');
  n = atoi(c+2);
  if(print)
    printf("ntas(): %s", buf);
  return n;
}

void
test0()
{
  // 定义字符数组用于构建目录和文件名称
  char file[2];
  char dir[2];
  enum { N = 10, NCHILD = 3 };
  int m, n;

  // 初始化目录和文件名称
  dir[0] = '0';
  dir[1] = '\0';
  file[0] = 'F';
  file[1] = '\0';

  printf("start test0\n");
  for(int i = 0; i < NCHILD; i++){ // 循环创建 NCHILD 个子目录，进入子目录，创建文件
    dir[0] = '0' + i; // 更新子目录的名称；每个子目录的名称从 '00' 到 '02'
    mkdir(dir);
    if (chdir(dir) < 0) { // 进入子目录
      printf("chdir failed\n");
      exit(1);
    }
    unlink(file); // 删除名为 'F' 的文件
    createfile(file, N); // 创建名为 'F' 的文件，其大小为 N 个块
    if (chdir("..") < 0) { // 返回上一级目录
      printf("chdir failed\n");
      exit(1);
    }
  }
  m = ntas(0); // 统计初始时文件系统的 I/O 操作次数
  for(int i = 0; i < NCHILD; i++){ // 使用 fork 创建 NCHILD 个子进程
    dir[0] = '0' + i; // 更新子目录的名称
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){ // 子进程
      if (chdir(dir) < 0) { // 进入相应的子目录
        printf("chdir failed\n");
        exit(1);
      }

      readfile(file, N*BSIZE, 1); // 读取名为 'F' 的文件，读取的块大小为 1

      exit(0); // 退出子进程
    }
  }

  // 等待所有子进程结束
  for(int i = 0; i < NCHILD; i++){
    wait(0);
  }
  printf("test0 results:\n");
  n = ntas(1);
  // 比较 n 和 m 的差值，如果小于 500，则输出 "test0: OK"，否则输出 "test0: FAIL"
  if (n-m < 500)
    printf("test0: OK\n");
  else
    printf("test0: FAIL\n");
}

void test1()
{
  int m = ntas(0); // 统计初始时文件系统的 I/O 操作次数
  char file[3];
  enum { N = 100, BIG=100, NCHILD=2 };
  
  printf("start test1\n");
  file[0] = 'B';
  file[2] = '\0';

  for(int i = 0; i < NCHILD; i++){
    file[1] = '0' + i;
    unlink(file);
    // 在第一次迭代中，创建一个大小为 BIG 的文件，其他迭代中创建大小为 1 的文件。
    if (i == 0) {
      createfile(file, BIG);
    } else {
      createfile(file, 1);
    }
  }

  for(int i = 0; i < NCHILD; i++){
    file[1] = '0' + i;
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){
      if (i==0) {
        // 第一个子进程（i == 0），则循环 N 次，每次读取 BIG 块，最后删除文件。
        // 这模拟了对一个大文件进行多次读取的情况。
        for (i = 0; i < N; i++) {
          readfile(file, BIG*BSIZE, BSIZE);
        }
        unlink(file);
        exit(0);
      } else {
        // 第二个子进程（i != 0），则循环 N 次，每次读取 1 块，最后删除文件。
        // 这模拟了对多个小文件进行多次读取的情况。
        for (i = 0; i < N; i++) {
          readfile(file, 1, BSIZE);
        }
        unlink(file);
      }
      exit(0);
    }
  }

  for(int i = 0; i < NCHILD; i++){
    wait(0);
  }

  int n = ntas(1);
  printf("test1 OK\n,IO counts %d",n-m);
}
