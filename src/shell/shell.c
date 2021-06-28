#include "shell.h"
#include "stdint.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"
#include "string.h"
#include "fs.h"
#include "buildin_cmd.h"

#define cmd_len 128     // 最大支持键入128个字符的命令行输入
#define MAX_CMD_LEN 512 // 输入的命令最长是512字节
#define MAX_ARG_NR 16   // 加上命令名外,最多支持15个参数

extern void hello(int argc, char const *argv[]);
extern void echo(int argc, char const *argv[]);

/* 存储输入的命令 */
static char cmd_line[cmd_len] = {0};

/* 用来记录当前目录,是当前目录的缓存,每次执行cd命令时会更新此内容 */
char cwd_cache[64] = {0};

/* 输出提示符 */
void print_prompt(void)
{
    printf("[imcgr@localhost %s]$ ", cwd_cache);
}

/* 从键盘缓冲区中最多读入count个字节到buf。*/
static void readline(char *buf, int32_t count)
{
    assert(buf != NULL && count > 0);
    char *pos = buf;
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count)
    { // 在不出错情况下,直到找到回车符才返回
        switch (*pos)
        {
            /* 找到回车或换行符后认为键入的命令结束,直接返回 */
        case '\n':
        case '\r':
            *pos = 0; // 添加cmd_line的终止字符0
            putchar('\n');
            return;

        case '\b':
            if (buf[0] != '\b')
            {          // 阻止删除非本次输入的信息
                --pos; // 退回到缓冲区cmd_line中上一个字符
                putchar('\b');
            }
            break;
        /* ctrl+l 清屏 */
        case 'l' - 'a':
            /* 1 先将当前的字符'l'-'a'置为0 */
            *pos = 0;
            /* 2 再将屏幕清空 */
            clear();
            /* 3 打印提示符 */
            print_prompt();
            /* 4 将之前键入的内容再次打印 */
            printf("%s", buf);
            break;

        /* ctrl+u 清掉输入 */
        case 'u' - 'a':
            while (buf != pos)
            {
                putchar('\b');
                *(pos--) = 0;
            }
            break;
        /* 非控制键则输出字符 */
        default:
            putchar(*pos);
            pos++;
        }
    }
    printf("readline: can`t find enter_key in the cmd_line, max num of char is 128\n");
}
/*
    Description:
        主要就是过滤掉命令和参数之间前后的多余的空格
    Parameters:
        cmd_str: char*。原始输入的命令，待解析
        argv: char**。解析后的多个命令
        token: char。分割符，常见是空格（命令和参数用空格分隔）
    Return:
        argc: 解析后命令的个数（包括参数）
        -1: 如果失败
    Details
        比如原始命令是：ls -l
        那么, 命令解析如下：
            cmd_str = "ls -l"
            token = ' '
            argv[0] = "ls"
            argv[1] = "-l"
            argc = 2
*/
static int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    assert(cmd_str != NULL);
    // 设置默认解析后的命令都是空的
    for (int32_t arg_idx = 0; arg_idx < MAX_ARG_NR; arg_idx++)
    {
        argv[arg_idx] = NULL;
    }

    // 开始解析原始输入的命令
    char *next = cmd_str;
    int32_t argc = 0;
    /* 外层循环处理整个命令行 */
    while (*next)
    {
        /* 去除命令字或参数之间的空格 */
        while (*next == token)
        {
            next++;
        }

        // 如果有\0作为字符串的截止标志，就退出循环
        if (*next == 0)
        {
            break;
        }

        // 第argc个命令的开始位置
        argv[argc] = next;

        // 跳过第argc个命令的内容
        while (*next && *next != token)
        {
            next++;
        }

        // 第argc个命令结束位置
        if (*next)
        {
            // 将token字符替换为字符串结束符0,做为一个单词的结束,并将字符指针next指向下一个字符
            *next++ = 0;
        }

        // 避免argv数组访问越界,参数过多则返回0
        if (argc > MAX_ARG_NR)
        {
            return -1;
        }
        argc++;
    }
    return argc;
}

// 执行命令
static void cmd_execute(uint32_t argc, char **argv)
{
    if (strcmp(argv[0], "ps") == 0)
    {
        buildin_ps(argc);
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        buildin_clear(argc);
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        buildin_help();
    }
    else
    {
        int32_t pid = fork();
        if (pid)
        { // 父进程
            int32_t status;
            int32_t child_pid = wait(&status);
            if (child_pid == -1)
            {
                panic("my_shell: no child\n");
            }
            printf("\n");
            printf("The proccess with pid %d exited with status %d\n", child_pid, status);
        }
        else
        {
            if (strcmp(argv[0], "hello") == 0)
            {
                execv(argv[0], hello, argv);
            }
            else if (strcmp(argv[0], "echo") == 0)
            {
                execv(argv[0], echo, argv);
            }
            else
            {
                printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
                exit(-1);
            }
        }
    }
}
// argv是用户输入的命令或者参数使用空格分割开
char *argv[MAX_ARG_NR];
int32_t argc = -1;
/* 简单的shell */
void my_shell(void)
{
    cwd_cache[0] = '/';
    while (1)
    {
        print_prompt();
        memset(cmd_line, 0, MAX_CMD_LEN);
        readline(cmd_line, MAX_CMD_LEN);
        if (cmd_line[0] == 0)
        {
            // 若只键入了一个回车
            continue;
        }
        argc = -1;
        // 把用户输入的原始命令cmd_line，过滤掉多余空格，得到argv[]数组
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1)
        {
            printf("number of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }

        // 执行解析后的命令
        cmd_execute(argc, argv);
    }
    panic("my_shell: should not be here");
}
