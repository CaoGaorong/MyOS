[toc]

## 实现系统调用wait和exit

前面说了通用系统调用的实现细节，以及说了```getpid()```系统调用的实现：<a href="#">系统调用的实现</a>（<font color="red">**链接未填充**</font>）

下面来实现常见的系统调用wait和exit。

先说说exit，是用于程序退出的系统调用，关于这个系统调用，在这里有说过<a href="#">为什么C语言程序不需要调用exit系统调用？</a>（<font color="red">**链接未填充**</font>），在那篇文章说了为什么C语言主函数结束位置不需要使用exit系统调用，而汇编语言在主函数_start的结束位置，要使用exit系统调用，否则会出现Segmentation Fault。



所以这里就来实现内核级别的exit系统调用。

系统调用的实现方式已经讲过了，所以就不再讲理论方面了，直接讲代码。



### 一、为什么要系统调用exit和wait

#### 1. exit系统调用

系统调用exit很容易理解，就是进程退出，然后释放该进程占用的空间。

#### 2. wait系统调用

但是wait有什么用？

关键在于，父进程和子进程通信。父进程使用fork出来一个子进程，肯定是让子进程去干某件事情的，那么子进程把事情干完了，在退出前应该要把结果给父进程，所以父进程应该要和子进程通信。



那么父进程如何获取到子进程的返回值呢？



为了让父进程可以获取到子进程的返回值，所以有以下设定：

- **子进程执行exit系统调用退出后，该进程的PCB不会被回收（因为存放了返回值）**
- **父进程执行wait系统调用，进入阻塞状态，等子进程运行完毕，获取子进程的PCB（包含子进程的返回值），然后再释放该子进程的空间（父进程为子进程”收尸“）**
  - 如果子进程还在运行时，父进程就exit退出了，那么这些还在运行的子进程就成了 <font color="red"><b>孤儿进程</b></font>。
    - 后续会被init进程收养。（算是一种补救措施吧）
  - 如果父进程忘记调用wait去给子进程”收尸“，那么该子进程占用的PCB不会释放，就成了<font color="red"><b>僵尸进程</b></font>
    - 由于是子进程先退出的，父进程忘记”收尸“了，所以也没办法让init进程收养



综上，wait系统调用有两个作用：

- 进入阻塞状态，等待子进程运行完
- 获取子进程的返回值，顺带”收尸“



### 二、系统调用exit和wait要做哪些东西？

知道了系统调用exit和wait的作用，下面来说明下实现exit系统调用和wait系统调用要做哪些东西：



首先说exit系统调用，某个进程调用了exit系统调用：

- 把自己的子进程托付给init进程
  - 因为自己要退出了，如果子进程不托付给init进程，那么子进程就成孤儿进程了
- 释放自身的资源，出了PCB所在的物理页
  - 回收该进程动态分配的物理空间
    - 通过页表映射的物理空间
  - 回收该PCB的虚拟地址池占用的空间
- 如果当前是子进程，就唤醒让正在wait等待的父进程



wait系统调用：

- 找出执行了exit的子进程
  - 持续遍历所有线程链表，找到父进程的pid为当前pid，并且处于hanging状态的进程
- 如果子进程执行完了，就给子进程收尸，回收PCB所在的空间
- 如果子进程没执行完，就进入等待队列，等子进程exit时唤醒



总的来讲，调用exit时要兼顾正在等待的父进程，调用wait要兼顾还没有exit的子进程。



### 三、exit和wait的内核实现

#### 1. exit的内核实现

自顶向下描述，exit的内核级实现sys_exit如下所示：

```c
/*
    Description:
        exit系统调用的内核实现, 功能就是回收自己进程所占用的资源（PCB除外）。
    Details:
        如果该进程是父进程：
            - 如果该进程还有子进程，就需要主动把子进程送给init进程领养（因为父进程要退出了）
            - 回收自身占用的所有资源（除了PCB）
        如果该进程是子进程，还需要干额外的事情：
            - 让父进程给自己"收尸"，如果父进程在wait等待，就需要唤醒他
            - 把自己挂起

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

    /* 将自己挂起,等待父进程获取其status,并回收其pcb */
    thread_block(TASK_HANGING);
}
```

- 关键点在于执行的```release_prog_resource```函数，回收空间函数，如下面代码

```c
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
```

- 上述代码有点复杂，主要就是两件事：
  - 回收进程动态分配的内存（通过页表映射的内存空间）
  - 回收PCB上的虚拟地址池的位图占用的物理空间



#### 2. wait的内核实现

wait的内核实现如下：

```c
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

            /*********2. 删除该进程的PCB所占的物理页********/
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
            // 若子进程还未运行完,即还未调用exit,则将自己挂起,直到子进程在执行exit时将自己唤醒
            // 否则剩余子进程就成了孤儿进程了
            thread_block(TASK_WAITING);
        }
    }
}
```

- wait的内核级实现比较简单，关键在于找出已经exit的子进程，然后回收该进程的PCB占用的物理页



### 四、wait和exit系统调用的封装

上述实现了wait和exit的内核实现```sys_wait```和```sys_exit```，这两个函数都是处于0特权级下的，用户进程没办法直接调用，所以必须封装成系统调用。

关于系统调用的封装，上一节已经讲过了：<a href="#">系统调用的实现</a>（<font color="red">**链接未填充**</font>）.

这里再放出把函数```sys_getpid()```封装成```getpid()```的过程：

![getpid封装成系统调用的过程](https://gitee.com/imcgr/image_blog/raw/master/20210623211202.png)

- 上图里层虚线框的内容都是固定的，不用修改的
- 在封装新的系统调用时，只需要改动外层虚线框的三段代码即可。



所以我们在封装exit和wait系统调用也是如此，有了之前做好的基础，直接封装即可。

- 首先添加内核级```sys_exit```和```sys_wait```的实现，上面已经写了。

- 然后把内核级实现添加到```syscall_table```中

  - ```c
    syscall_table[SYS_EXIT] = sys_exit;
    syscall_table[SYS_WAIT] = sys_wait;
    ```

- 然后提供外层的系统调用：

  - ```c
    /* 以状态status退出 */
    void exit(int32_t status)
    {
        _syscall1(SYS_EXIT, status);
    }
    
    /* 等待子进程,子进程状态存储到status */
    pid_t wait(int32_t *status)
    {
        return _syscall1(SYS_WAIT, status);
    }
    ```



经过上述操作，就封装好了系统调用```exit```和```wait```。

