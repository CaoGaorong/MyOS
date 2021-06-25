#include "fork.h"
#include "process.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"
#include "string.h"

extern void intr_exit(void);

/* 将父进程的pcb、虚拟地址位图拷贝给子进程 */
/*
    Description:
        - 浅拷贝父进程的PCB所在物理页内容给子进程
            - PCB所在物理页包含了 用户栈 和 内核栈
        - 深拷贝拷贝父进程的虚拟地址池位图给子进程
    Parameters:
        child_thread: struct task_struct*。子进程PCB
        parent_thread: struct task_struct*。父进程PCB
    Returns:
        成功返回0,失败返回-1
    Details:
        先浅拷贝父进程PCB所在物理页，这样拷贝了PCB和用户栈和内核栈
        然后拷贝父进程虚拟地址池的内容
*/
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    // 1. 拷贝PCB所在的物理页
    memcpy(child_thread, parent_thread, PG_SIZE);
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority; // 为新进程把时间片充满
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
    block_desc_init(child_thread->u_block_desc);
    // 2. 深拷贝父进程的虚拟地址池的位图
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    void *vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
    if (vaddr_btmp == NULL)
        return -1;

    memcpy(vaddr_btmp, child_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
    child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;

    ASSERT(strlen(child_thread->name) < 11); // pcb.name的长度是16,为避免下面strcat越界
    // 默认子进程名字是"父进程_fork"，比如 "init_fork"
    strcat(child_thread->name, "_fork");
    return 0;
}

/*
    Description:
        把父进程的虚拟地址池中位图为1的位对应的物理内存，拷贝给子进程
    Parameters:
        child_thread: 子进程
        parent_thread: 父进程
        buf_page: 缓冲区，临时存放1个物理页的空间
    Details:
        1. 先扫描父进程中位图，哪些位被占用了，即可得出哪些内存用过了
        2. 把父进程中被占用的内存页，复制到缓冲区buf_page中
        3. 然后从buf_page内容复制到子进程中

        注意：子进程的虚拟地址要和父进程的相同，但是物理地址不能相同
*/
static void copy_body_stack3(struct task_struct *child_thread, struct task_struct *parent_thread, void *buf_page)
{
    // 父进程的虚拟地址池位图
    uint8_t *vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
    // 父进程位图长度（占用字节数）
    uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
    // 父进程位图表示的起始虚拟地址
    uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;

    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t prog_vaddr = 0;

    /************* 1. 查找父进程的用户空间中已有数据的页************/
    while (idx_byte < btmp_bytes_len)
    {
        // 先按照字节查找
        if (vaddr_btmp[idx_byte])
        {
            // 再按照位查找
            idx_bit = 0;
            while (idx_bit < 8)
            {
                /***********2. 拷贝父进程的物理页到子进程中*************/
                /*但是要注意，拷贝后，父进程和子进程的虚拟地址相同，但是物理地址不同*/
                if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte])
                {
                    // 此时的prog_vaddr是父进程的虚拟页地址
                    prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;

                    // 把该父进程的虚拟页对应的物理页内容，拷贝到buf_page中
                    memcpy(buf_page, (void *)prog_vaddr, PG_SIZE);

                    // 把子进程的页表激活，即cr3寄存器安装为子进程的页表
                    page_dir_activate(child_thread);

                    // 页表切换之后，prog_vaddr就表示子进程的虚拟地址了，操作的就是子进程的虚拟地址
                    // 给prog_vaddr这个虚拟地址分配1个物理页
                    get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);

                    // 然后把父进程的数据复制给子进程
                    memcpy((void *)prog_vaddr, buf_page, PG_SIZE);

                    // 恢复父进程页表
                    page_dir_activate(parent_thread);
                }
                idx_bit++;
            }
        }
        idx_byte++;
    }
}

/*
    Description:
        构建子进程的用户栈，让fork之后的子进程可以运行
    Details:
        关键点如下：
            - fork是系统调用，所以执行fork时，在0特权级
                - 所以要想运行子进程，所以子进程也要从0特权级到3特权级，
                - 因此也要借用中断退出intr_exit函数的iret指令
            - 但是fork进程和创建进程还是不一样的
                - 创建进程时，需要构建中断栈，然后借用intr_exit来执行
                - 而fork进程，由于子进程已经把父进程的中断栈拷过来了，不需要构建中断栈了
*/
static int32_t build_child_stack(struct task_struct *child_thread)
{
    // 获取子进程0级栈栈顶
    struct intr_stack *intr_0_stack = (struct intr_stack *)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
    // 修改子进程的返回值为0
    intr_0_stack->eax = 0;

    /*
        用户栈中只剩下如下数据，即ret（恢复上下文时会被执行的数据）、esi、edi、ebx、ebp
        为什么没有了其他数据？
            - 由于该子进程的用户栈是父进程拷贝过来的
            - 所以栈中有的数据没了，比如func_arg、 function、unused_retaddr
            - 因为这些数据只有进程刚被创建，第一次被调度时才有用
    */
    uint32_t *ret_addr_in_thread_stack = (uint32_t *)intr_0_stack - 1;
    uint32_t *esi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 2;
    uint32_t *edi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 3;
    uint32_t *ebx_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 4;
    uint32_t *ebp_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 5;

    /* switch_to的返回地址更新为intr_exit,直接从中断返回 */
    // 这样该进程被调度时，就会进入intr_exit，借用iret指令从0特权级恢复到3特权级
    *ret_addr_in_thread_stack = (uint32_t)intr_exit;

    // 下面数据可以不要
    *ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack = 0;
    *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;

    // 把构建的thread_stack的栈顶做为switch_to恢复数据时的栈顶
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}

/*
    Description:
        拷贝父进程的资源给子进程
    Parameters:
        child_thread: 子进程
        parent_thred: 父进程
    Return:
        -1: 失败
        0: 成功
    Details:
        拷贝父进程资源给子进程分为以下几个步骤：
            1. 拷贝父进程的pcb所在物理页、虚拟地址池位图
                - pcb所在物理页包括了用户栈、内核栈
            2. 拷贝父进程用到的物理页资源给子进程（页表以及映射到的物理页）
                - 扫描父进程的位图，查看父进程用到了哪些空间
                - 然后把用到的空间拷贝给子进程
                - 注意，拷贝后，父进程和子进程的虚拟地址要相同，物理地址要不同
            3. 构建子进程的用户栈/线程栈/intr_stack
                - 子进程创建时，是进入内核空间，发起的系统调用，特权级是0级
                - 所以我们构建子进程的线程栈的eip位置内容，设置为intr_exit中断退出
                - 那么就可以借用中断退出的iret指令，回到3特权级
                - 这样子进程才可以正常运行
*/
static int32_t copy_process(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    // 内核缓冲区,作为父进程用户空间的数据复制到子进程用户空间的中转
    void *buf_page = get_kernel_pages(1);
    if (buf_page == NULL)
    {
        return -1;
    }

    // 1. 复制父进程的pcb所在的物理页（包括中断栈、线程栈）、虚拟地址位图给子进程
    if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1)
    {
        return -1;
    }

    // 为子进程创建页表,此页表仅包括内核空间
    // create_page_dir创建页表，不仅包含了页表的768-1022项，还有1023项指向页表自身
    child_thread->pgdir = create_page_dir();
    if (child_thread->pgdir == NULL)
    {
        return -1;
    }

    // 2. 复制父进程进程体（用到的物理页）及用户栈给子进程
    copy_body_stack3(child_thread, parent_thread, buf_page);

    // 3. 构建子进程thread_stack和修改fork返回值
    build_child_stack(child_thread);

    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}

/*
    Description:
        由用户进程调用sys_fork，拷贝出一个子进程，并返回子进程的pid
    Parameters:
        void
    Return:
        -1: 失败
        子进程pid：成功
    Details:
        - 拷贝父进程（当前进程）的资源给子进程
        - 把子进程加入就绪队列和全部线程队列
*/
pid_t sys_fork(void)
{
    struct task_struct *parent_thread = running_thread();
    struct task_struct *child_thread = get_kernel_pages(1); // 为子进程创建pcb(task_struct结构)
    if (child_thread == NULL)
    {
        return -1;
    }
    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);

    if (copy_process(child_thread, parent_thread) == -1)
    {
        return -1;
    }

    /* 添加到就绪线程队列和所有线程队列,子进程由调试器安排运行 */
    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
    list_append(&thread_ready_list, &child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);

    return child_thread->pid; // 父进程返回子进程的pid
}
