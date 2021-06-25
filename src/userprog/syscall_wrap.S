[bits 32]
extern exit
section .text
global syscall_wrap

; Description:
;   这是普通进程的库。某个进程函数func在执行前，需要参数argc和argv，在这里syscall_wrap
;   会把这两个参数从ebx和ecx中压入栈中，给func使用，而func在edx中
; Details:
;   - 约定：自定义函数的入口地址（类似C程序的main函数）在edx中

syscall_wrap:
    
    ; 下面这两个要和 execv 中 load 之后指定的寄存器一致
	push ebx             ; 压入 argv
	push ecx             ; 压入 argc
	
    call edx          ; 获取获取自定义函数的入口

    ; 将 main 的返回值通过栈传给 exit, gcc 用 eax 存储返回值, 这是 ABI 规定的
	push eax
	call exit