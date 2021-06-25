#include "buildin_cmd.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "shell.h"
#include "assert.h"

/* ps命令内建函数 */
void buildin_ps(uint32_t argc)
{
    if (argc != 1)
    {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}

/* clear命令内建函数 */
void buildin_clear(uint32_t argc)
{
    if (argc != 1)
    {
        printf("clear: no argument support!\n");
        return;
    }
    clear();
}

/* clear命令内建函数 */
void buildin_help(void)
{
    help();
}
