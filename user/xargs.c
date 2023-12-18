#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define STDIN 0
#define MAXLEN 512
#define MAXARG 32  // max exec arguments


int main(int argc, char *argv[])
{
    char buf[MAXLEN];
    char *cmd;
    char *params[MAXARG];
    int n;

    if (argc < 2)
    {
        fprintf(2, "Usage: xargs <your_cmd>\n");
        exit(0);
    }

    if (argc + 1 > MAXARG)
    {
        fprintf(2, "too many args\n");
        exit(0);
    }

    cmd = argv[1]; // get command

    for( int i = 1; i < argc ; i++) // get params
    {
        params[i - 1] = argv[i];
    }

    while(1)
    {
        int index = 0;
        // read a line from standard input
        while(1)
        {
            n = read(STDIN, &buf[index], 1); // read a char each time
            if (n == 0) break; 
            if (n < 0) 
            {
                fprintf(2, "read error\n");
                exit(0);
            }
            if (buf[index] == '\n')
            {
                break;
            }
            index++;
        }
        //
        if (index == 0) break;

        buf[index] = '\0';

        params[argc - 1] = buf;

        if ( fork() == 0) // child process
        {
            exec(cmd, params);
            exit(0);
        }
        else // parent process
        {
            wait(0);
        }
    }
    exit(0);

}