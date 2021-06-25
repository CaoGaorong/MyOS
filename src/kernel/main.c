#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "shell.h"
#include "assert.h"
#include "stdio-kernel.h"


typedef int (*gernal_func)(int argc, char const *argv[]);

void init(void);


int main(void)
{
    put_str("I am kernel\n");
    init_all();
    cls_screen();

    // main函数退出
    thread_exit(running_thread(), true);
}

/* init进程 */
void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid)
    {
        int status;
        int child_pid;
        // init 子进程收尸
        while (1)
        {
            child_pid = wait(&status);
            printf("I'm init, my pid is 1, I receive a child, it's pid is %d, status is %d\n", child_pid, status);
        }
    }
    else
    {
        // 子进程
        my_shell();
    }
    panic("init: should not be here");
}


int hello(int argc, char const *argv[])
{
    printf("Hello, World\n");
    return 0;
}
int echo(int argc, char const *argv[])
{
    for (uint32_t i = 1; i < argc; i++)
    {
        printf("%s ", argv[i]);
    }
    printf("\n");
    return 0;
}



