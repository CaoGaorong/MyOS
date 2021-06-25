#ifndef __SHELL_BUILDIN_CMD_H
#define __SHELL_BUILDIN_CMD_H
#include "stdint.h"
void buildin_ps(uint32_t argc);
void buildin_clear(uint32_t argc);
void buildin_help(void);
#endif