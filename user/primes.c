#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define PIPE_W 1
#define PIPE_R 0

void* child(int* p)
{
    // read from left neighbor
    close(p[PIPE_W]);
    int num;
    if (read(p[PIPE_R], &num, sizeof(num)) == 0) 
    {
        close(p[PIPE_R]);
        exit(0);
    }
    printf("prime %d\n", num); // 如果能读出来数字，就输出现在读到的数字。

    int cp[2];
    pipe(cp);
    // write to right neighbor

    if (fork() == 0) // right neighbor
    {
        child(cp);
    }
    else // this process
    {
        close(cp[PIPE_R]);
        int nnum;
        while(read(p[PIPE_R], &nnum, sizeof(nnum))) // process 1 write an number, then process 2 read from the pipe
        {
            if (nnum % num != 0) // 如果读取到的不是当前 num的倍数，就写入管道，让子进程读取。
            {
                write(cp[PIPE_W], &nnum, sizeof(nnum));
            }
        }
        close(cp[PIPE_W]);
        wait(0);
    }

    exit(0);
}

int main(int argc, char const *argv[])
{
    int p[2];
    pipe(p);

    if (fork() == 0) // child process
    {
       child(p);
    }
    else // parent process
    {
        close(p[PIPE_R]);

        for (int i = 2; i <= 35; i++)
        {
            write(p[PIPE_W], &i, sizeof(i)); // 往管道写入整数i
        }
        close(p[PIPE_W]);   
        wait(0); // 等待子进程结束

    }

    exit(0);
}