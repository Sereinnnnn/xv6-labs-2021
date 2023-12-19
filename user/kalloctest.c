#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NCHILD 2
#define N 100000
#define SZ 4096

void test1(void);
void test2(void);
char buf[SZ];

int
main(int argc, char *argv[])
{
  test1();
  test2();
  exit(0);
}

// 获取系统内存统计信息，并记录起始的内存分配数量。
int ntas(int print)
{
  int n;
  char *c;

  if (statistics(buf, SZ) <= 0) {
    fprintf(2, "ntas: no stats\n");
  }
  c = strchr(buf, '=');
  n = atoi(c+2);
  if(print)
    printf("%s", buf);
  return n;
}

// 测试 fork 和 sbrk 的组合使用，检查进程的内存分配和释放是否正常。
void test1(void)
{
  void *a, *a1;
  int n, m;
  printf("start test1\n");  
  m = ntas(0);
  for(int i = 0; i < NCHILD; i++){ // 创建 NCHILD 个子进程
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){
      for(i = 0; i < N; i++) {
        a = sbrk(4096); // 用 sbrk 分配 4096 字节内存
        *(int *)(a+4) = 1; // 写入一个整数到分配的内存中
        a1 = sbrk(-4096); // 分配后，再使用 sbrk 释放相同的内存量
        if (a1 != a + 4096) { // 检查释放后的内存地址是否正确
          printf("wrong sbrk\n");
          exit(-1);
        }
      }
      exit(-1);
    }
  }

  for(int i = 0; i < NCHILD; i++){
    wait(0);
  }
  printf("test1 results:\n");
  n = ntas(1); // 再次调用 ntas 获取系统内存统计信息，并计算内存分配的变化
  if(n-m < 10) // 根据内存分配变化判断测试是否通过
    printf("test1 OK\n");
  else
    printf("test1 FAIL\n");
}

//
// countfree() from usertests.c
//
int
countfree()
{
  uint64 sz0 = (uint64)sbrk(0);
  int n = 0;

  while(1){
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      break;
    }
    // modify the memory to make sure it's really allocated.
    *(char *)(a + 4096 - 1) = 1;
    n += 1;
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}

// 测试系统的总体内存分配和释放是否正常
void test2() {
  int free0 = countfree(); // 用 countfree 函数获取当前空闲页面数，并记录为 free0
  int free1;
  int n = (PHYSTOP-KERNBASE)/PGSIZE;
  printf("start test2\n");  
  printf("total free number of pages: %d (out of %d)\n", free0, n);
  if(n - free0 > 1000) {
    printf("test2 FAILED: cannot allocate enough memory");
    exit(-1);
  }
  for (int i = 0; i < 50; i++) { // 循环 50 次，每次调用 countfree 获取当前空闲页面数，并与 free0 进行比较，检查是否有页面丢失
    free1 = countfree();
    if(i % 10 == 9)
      printf(".");
    if(free1 != free0) {
      printf("test2 FAIL: losing pages\n");
      exit(-1);
    }
  }
  printf("\ntest2 OK\n"); // 如果没有丢失，判定测试通过
}


