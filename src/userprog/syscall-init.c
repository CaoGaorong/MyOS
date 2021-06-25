#include "syscall-init.h"
#include "syscall.h"
#include "stdint.h"
#include "print.h"
#include "thread.h"
#include "console.h"
#include "string.h"
#include "memory.h"
#include "fork.h"
#include "wait_exit.h"
#include "fs.h"
#include "exec.h"

#define syscall_nr 32
typedef void *syscall;
syscall syscall_table[syscall_nr];

/* 返回当前任务的pid */
uint32_t sys_getpid(void)
{
   return running_thread()->pid;
}

/* 初始化系统调用 */
void syscall_init(void)
{
   put_str("syscall_init start\n");
   syscall_table[SYS_GETPID] = sys_getpid;
   syscall_table[SYS_WRITE] = sys_write;
   syscall_table[SYS_MALLOC] = sys_malloc;
   syscall_table[SYS_FREE] = sys_free;
   syscall_table[SYS_FORK] = sys_fork;
   syscall_table[SYS_READ] = sys_read;
   syscall_table[SYS_PUTCHAR] = sys_putchar;
   syscall_table[SYS_CLEAR] = cls_screen;
   syscall_table[SYS_PS]	 = sys_ps;
   syscall_table[SYS_EXIT] = sys_exit;
   syscall_table[SYS_WAIT] = sys_wait;
   syscall_table[SYS_HELP] = sys_help;
   syscall_table[SYS_EXECV] = sys_execv;
   
   put_str("syscall_init done\n");
}
