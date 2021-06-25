#include "fs.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"

/*
    Description:
        输出函数（目前仅支持在控制台输出）
    Parameters:
        fd: 文件描述符，目前只支持stdout_no，即控制台标准输出
        buf: 缓冲区，把buf的内容输出
        count: 输出最大容量
    Return:
        count: 如果成功
        -1 : 如果失败
*/
int32_t sys_write(int32_t fd, const void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_write: fd error\n");
        return -1;
    }
    if (fd == stdout_no)
    {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    console_put_str("sys_write: only support stdou_no\n");
    return -1;
}
/*
    Description:
        从文件描述符fd指向的文件中读取count个字节到buf
    Parameters:
        fd: 文件描述符，目前仅支持stdin_no，即标准输入，从控制台中输入
        buf: 内容读取到buf中储存
        count: 读取的最大个数
    Returns:
        成功则返回读取的个数
        失败则返回-1
*/
int32_t sys_read(int32_t fd, void *buf, uint32_t count)
{
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0 || fd == stdout_no || fd == stderr_no)
    {
        printk("sys_read: fd error\n");
    }
    else if (fd == stdin_no)
    {
        char *buffer = buf;
        uint32_t bytes_read = 0;
        while (bytes_read < count)
        {
            *buffer = ioq_getchar(&kbd_buf);
            bytes_read++;
            buffer++;
        }
        ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
    }
    // 暂时不支持读取磁盘内容，因为没有文件系统
    else
    {
        printk("sys_read: not support to read from disk\n");
    }
    return ret;
}

/* 显示系统支持的内部命令 */
void sys_help(void)
{
    printk("buildin commands:\n");
    printk("    ps: process status\n");
    printk("    clear: clear screen\n");
    printk("\n");
    printk("buildin processes:\n");
    printk("    hello: say \"Hello, World\"\n");
    printk("    echo: display a line of text\n");
    printk("\n");
    printk("shortcut keys:\n");
    printk("    ctrl+l: clear screen\n");
    printk("    ctrl+u: clear input\n");
}

/* 向屏幕输出一个字符 */
void sys_putchar(char char_asci)
{
    console_put_char(char_asci);
}