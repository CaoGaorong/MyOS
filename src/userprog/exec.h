#ifndef __USERPROG_EXEC_H
#define __USERPROG_EXEC_H
#include "stdint.h"
int32_t sys_execv(const char* name, void* func, const char* argv[]);
#endif
