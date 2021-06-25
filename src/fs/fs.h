#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"

int32_t sys_write(int32_t fd, const void *buf, uint32_t count);
int32_t sys_read(int32_t fd, void* buf, uint32_t count);
void sys_putchar(char char_asci);
void sys_help(void);
/* 标准输入输出描述符 */
enum std_fd {
   stdin_no,   // 0 标准输入
   stdout_no,  // 1 标准输出
   stderr_no   // 2 标准错误
};
    


#endif