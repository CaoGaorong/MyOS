#include "exec.h"
#include "thread.h"    
#include "stdio-kernel.h"
#include "fs.h"
#include "string.h"
#include "global.h"
#include "memory.h"

// 进程名最大16字节
# define TASK_NAME_LEN 16

extern void syscall_wrap();

/*
    Description:
        执行名称为path的函数（作为进程）
    Parameters:
        name: 进程名称
        func: 自定义的函数地址
        argv: 参数列表
    Details:
        - 自定义函数地址放入edx中保存
        - 定义函数func会经过syscall_warp包装
        - 在syscall_warp中取出edx即func地址，然后调用call func
*/
int32_t sys_execv(const char* name, void* func, const char* argv[]) {
    uint32_t argc = 0;
    while (argv[argc]) {
        argc++;
    }
    int32_t entry_point = (int32_t)syscall_wrap;

    struct task_struct* cur = running_thread();
    // 修改进程名
    memcpy(cur->name, name, TASK_NAME_LEN);
    cur->name[TASK_NAME_LEN-1] = 0;

    struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
    // 参数传递给用户进程
    intr_0_stack->ebx = (int32_t)argv;
    intr_0_stack->ecx = argc;
    // eip存放程序入口地址（相当于C程序的_start）
    intr_0_stack->eip = (void*)entry_point;
    // edx存放程序的开始地址（相当于C程序的main）
    intr_0_stack->edx = (void*)func;
    // 使新用户进程的栈地址为最高用户空间地址
    intr_0_stack->esp = (void*)0xc0000000;

    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (intr_0_stack) : "memory");
    return 0;
}
