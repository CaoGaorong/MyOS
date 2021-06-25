#include "wait_exit.h"
#include "global.h"
#include "debug.h"
#include "thread.h"
#include "list.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "bitmap.h"

/*
	Description:
		回收某个进程的资源
	Prameters:
		release_thread: 要被释放的进程
	Details:
		- 回收该进程动态分配的物理空间
			- 通过页表映射的物理空间
		- 回收该PCB的虚拟地址池占用的空间
		- 自身PCB所占用的物理页并没有释放（还包括了用户栈和内核栈）
*/
static void release_prog_resource(struct task_struct *release_thread)
{
    uint32_t *pgdir_vaddr = release_thread->pgdir;
    uint16_t user_pde_nr = 768, pde_idx = 0;
    uint32_t pde = 0;
    uint32_t *v_pde_ptr = NULL; // v表示var,和函数pde_ptr区分

    uint16_t user_pte_nr = 1024, pte_idx = 0;
    uint32_t pte = 0;
    uint32_t *v_pte_ptr = NULL; // 加个v表示var,和函数pte_ptr区分

    uint32_t *first_pte_vaddr_in_pde = NULL; // 用来记录pde中第0个pte的地址
    uint32_t pg_phy_addr = 0;

    /***************************1. 回收页表中用户空间的页框*****************************************/
    while (pde_idx < user_pde_nr)
    {
        // 指向页目录表项的虚拟地址，通过该虚拟地址可以访问该页目录表项
        v_pde_ptr = pgdir_vaddr + pde_idx;
        // 取出该页目录表项的内容，就是页表的地址
        pde = *v_pde_ptr;
        // 页目录表项（也就是页表的地址）最低位是P位
        if (pde & 0x00000001)
        {
            // 一个页表表示的内存容量是4M,即0x400000
            // 返回pde_idx * 0x400000 必经的页表项 的虚拟地址
            // first_pte_vaddr_in_pde,可以访问到 pde_idx * 0x400000 必经的页表项
            first_pte_vaddr_in_pde = pte_ptr(pde_idx * 0x400000);
            pte_idx = 0;
            while (pte_idx < user_pte_nr)
            {
                // 指向页表项的虚拟的地址，通过该虚拟地址可以访问到该页表项
                v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
                // 取出该页表项的内容，也就是物理页的物理地址
                pte = *v_pte_ptr;
                // 如果页表项的P位是1,说明连接了物理页
                // pte页表项的内容是物理页的物理地址
                if (pte & 0x00000001)
                {
                    // 页表项的高20位就是物理页的地址
                    // 因为一个物理页占4KB，只需要高20位就可以表示了
                    pg_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(pg_phy_addr);
                }
                pte_idx++;
            }
            // 页目录表项的高20位，就是页表的物理地址
            // 因为一个页表占用一个物理页4KB，只需要20位就可以表示了
            pg_phy_addr = pde & 0xfffff000;
            free_a_phy_page(pg_phy_addr);
        }
        pde_idx++;
    }

    /***************************2. 虚拟地址池占用的位图*****************************************/
    uint32_t bitmap_pg_cnt = (release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len) / PG_SIZE;
    uint8_t *user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);
}

/* list_traversal的回调函数,
 * 查找pelem的parent_pid是否是ppid,成功返回true,失败则返回false */
static bool find_child(struct list_elem *pelem, int32_t ppid)
{
    /* elem2entry中间的参数all_list_tag取决于pelem对应的变量名 */
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid)
    {                // 若该任务的parent_pid为ppid,返回
        return true; // list_traversal只有在回调函数返回true时才会停止继续遍历,所以在此返回true
    }
    return false; // 让list_traversal继续传递下一个元素
}

/* list_traversal的回调函数,
 * 查找状态为TASK_HANGING的任务 */
static bool find_hanging_child(struct list_elem *pelem, int32_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING)
    {
        return true;
    }
    return false;
}

/* list_traversal的回调函数,
 * 将一个子进程过继给init */
static bool init_adopt_a_child(struct list_elem *pelem, int32_t pid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == pid)
    { // 若该进程的parent_pid为pid,返回
        pthread->parent_pid = 1;
    }
    return false; // 让list_traversal继续传递下一个元素
}

/*
    Description:
        等待子进程调用wait，接受子进程返回值，释放子进程PCB
    Parameters:
        status: int32_t*。 把子进程的pid保存到status指向的内存空间
    Return:
        child_pid: 子进程的pid
        -1： 失败
    Details:
        - 持续遍历所有线程链表，找到父进程的pid为当前pid，并且处于hanging状态的进程
        - 获取出该进程的退出状态，保存起来
        - 给该进程收尸，回收PCB所在的空间
*/
pid_t sys_wait(int32_t *status)
{
    struct task_struct *parent_thread = running_thread();

    while (1)
    {
        /*********1. 找到父进程pid为当前进程pid，且处于hanging状态的进程*********/
        struct list_elem *child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
        // 如果有符合条件的进程
        if (child_elem != NULL)
        {
            // 取出该进程
            struct task_struct *child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
            // 保存该进程的退出状态
            *status = child_thread->exit_status;

            // 把子进程的pid也取出来
            uint16_t child_pid = child_thread->pid;

            /*********2. 删除该进程的PCB********/
            thread_exit(child_thread, false); // 传入false,使thread_exit调用后回到此处
            return child_pid;
        }

        // 判断是否有子进程
        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        if (child_elem == NULL)
        { // 若没有子进程则出错返回
            return -1;
        }
        else
        {
            /* 若子进程还未运行完,即还未调用exit,则将自己挂起,直到子进程在执行exit时将自己唤醒 */
            thread_block(TASK_WAITING);
        }
    }
}

/*
    Description:
        exit系统调用的内核实现, 功能就是回收自己进程所占用的资源（PCB除外）。
    Details:
        如果该进程是父进程：
            - 如果该进程还有子进程，就需要主动把子进程送给init进程领养（因为父进程要退出了）
            - 回收自身占用的所有资源（除了PCB）
        如果该进程是子进程，还需要干额外的事情：
            - 让父进程给自己“收尸“，如果父进程在wait等待，就需要唤醒他

*/
void sys_exit(int32_t status)
{
    // 获取当前进程
    struct task_struct *child_thread = running_thread();
    // 设置退出状态，也就是返回值
    child_thread->exit_status = status;
    if (child_thread->parent_pid == -1)
    {
        PANIC("sys_exit: child_thread->parent_pid is -1\n");
    }

    // 将进程child_thread的所有子进程都过继给init
    list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

    // 回收该进程占用的资源（PCB除外）
    release_prog_resource(child_thread);

    /* 如果父进程正在等待子进程退出,将父进程唤醒 */
    struct task_struct *parent_thread = pid2thread(child_thread->parent_pid);
    if (parent_thread->status == TASK_WAITING)
    {
        thread_unblock(parent_thread);
    }

    // 将自己挂起,等待父进程获取其status,并回收其pcb
    thread_block(TASK_HANGING);
}
