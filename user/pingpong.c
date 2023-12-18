#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define PIPE_W 1
#define PIPE_R 0

/*
    在读写管道时，read端会等到write端写完才会开始读。因此可以利用读写顺序控制依次输出pingpong。
    因此，按照下方代码，父进程应先执行写操作，子进程读等待结束后，输出"ping"。
    随后子进程向管道执行写操作，父进程等待子进程写入之后执行读操作，输出“pong”。
    如果父子进程都先读后写，则两个进程都因为写端仍未开始写而导致无法read，互相等待。
    如果父子进程都先写后读，则两个进程交替输出，结果交错杂乱。
    
    交替发送 "ping" 和 "pong" 消息，并在控制台上打印相应的信息。
    由于是基于管道的进程通信，涉及到阻塞和同步的概念，程序的执行结果可能会有一些不确定性。

    关于管道通信：https://www.cnblogs.com/whiteHome/p/4863516.html
        1. 调用 pipe函数之后，p2c[0]代表读端，p2c[1]代表写端（就像0是标准输入1是标准输出一样）
        2. 父进程调用 pipe 开辟管道，得到两个文件描述符指向管道的两端。 
           父进程调用 fork 创建子进程，那么子进程也有两个文件描述符指向同一管道。
        3. 父进程关闭管道读端，子进程关闭管道写端。父进程可以往管道里写，子进程可以从管道里读，
*/

#define MAXSIZE 64

int main(int argc, char const *argv[])
{
    int p2c[2]; // parent to child
    int c2p[2]; // child to parent

    pipe(p2c);
    pipe(c2p);

    int pid = 0;
    pid = fork(); // 在父进程中，pid 保存子进程的 PID，在子进程中，pid 的值为 0。

    char message[MAXSIZE]; // 存字符串

    if (pid == 0) { // child
        close(p2c[PIPE_W]); // 是子进程这边关了，子进程不会往这个共用的管道里面写东西，它只从这里面读取；但父进程的写端没有关闭，可以继续写。
        close(c2p[PIPE_R]);

        // 这是一个阻塞操作，直到父进程写入数据；如果父进程写完了，或者关闭了写端，子进程会在 read 操作中得到相应的信号
        if (read(p2c[PIPE_R], message, MAXSIZE) > 0) { // read 和 write 的返回值应该是实际读取或写入的字节数
            printf("%d: received %s\n", getpid(), message);
        }
        write(c2p[PIPE_W], "pong", strlen("pong")); // 第三个参数应该使用 strlen 获取字符串的实际长度，而不是固定的 MAXSIZE。这样可以避免发送多余的空白字符。
        exit(0);      
    }
    else { // parent
        close(c2p[PIPE_W]);
        close(p2c[PIPE_R]);

        write(p2c[PIPE_W], "ping", strlen("ping"));

        if (read(c2p[PIPE_R], message, MAXSIZE) > 0) {
            printf("%d: received %s\n", getpid(), message);
        }
    }
    exit(0);
}