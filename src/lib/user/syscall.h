#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"
#include "thread.h"

enum SYSCALL_NR
{
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
    SYS_FORK,
    SYS_READ,
    SYS_PUTCHAR,
    SYS_PS,
    SYS_EXIT,
    SYS_WAIT,
    SYS_CLEAR, // 对应cls_screen函数
    SYS_HELP, // shell的help命令
    SYS_EXECV
};

uint32_t getpid(void);

uint32_t write(int32_t fd, const void* buf, uint32_t count);

void *malloc(uint32_t size);

void free(void *ptr);

int16_t fork(void);

int32_t read(int32_t fd, void *buf, uint32_t count);

void putchar(char char_asci);
void ps(void);

int execv(const char* name, void* func, char** argv);

void exit(int32_t status);
pid_t wait(int32_t *status);

void clear(void);

// 以下系统调用是给shell专用的
void help(void);
#endif
