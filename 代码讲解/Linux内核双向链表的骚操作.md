## Linux系统内核队列的骚操作

本系统仿照Linux中的队列实现是一个双向链表，个人认为Linux中的双向链表实现简直太妙了。

### 一、常规链表的实现

在学习数据结构课程时，都学过双向链表这种数据结构，基本上都是下面这种结构：

```c
struct student {
    struct student* next;
    int id;
    char name[30];
	struct student* pre;
};
```

很多节点组成的双向链表结构大致如下：

![](https://gitee.com/imcgr/image_blog/raw/master/20210613123233.png)



> 我特意在每个结构体周围花了虚线的边框，表示整个结构体。（并不表示结构体本身和成员有空隙）。

会发现，这样的链表有一下很重要的特点：

- 节点指针(pre和next)指向一个**整个节点**



但是会发现，这样的链表有很大的局限性：

- 不同的链表的节点的结构和操作都是类似了（无非增删改查），只有链表节点中的数据不同
- 每当创建一个不同类型的链表，都要重新写一遍（包括链表的操作）



但是在Linux中，实现了一种更加通用的链表结构，简直太妙了。

### 二、Linux中的链表实现

在Linux中的双向链表的实现是这样的（听说，我也没看过Linux源代码）。

#### 1. 链表节点的结构

下面是**链表的节点的结构**：

```c
/*结点中不需要数据成元,只要求前驱和后继结点指针*/
struct list_elem {
   struct list_elem* prev; // 前躯结点
   struct list_elem* next; // 后继结点
};
```

乍一看，总感觉怪怪的，怪就对了：**节点中没有放数据**。

没有数据的链表有什么用呢？这些节点串起来一点用也没有啊？

#### 2. 数据节点

<font color="red">常规链表中，把数据放在链表节点里面。**Linux中，把链表节点放到数据里面。**</font>

直接上代码演示吧，如下是一个学生节点：

```c
struct student {
	int id;
	char name[30];
	struct list_elem tag;
};
```

看到这里，你应该大概懂了，数据节点怎样组成链表了，基本上如下图所示：

![](https://gitee.com/imcgr/image_blog/raw/master/20210613121638.png)

我们可以知道，这样的链表有一下几个特性：

- 链表节点 (list_elem结构体) 作为数据节点 (student结构体) 的成员
- 数据节点(student结构体)之间，是通过它的成员tag，来串起来的
- <font color="red">每个链表节点，只指向下一个链表节点</font>



这个时候问题来了，数据节点是串起来了，但是怎么访问呢？

一般我们会遍历链表，可是最终我们**需要的是数据**啊，这样的链表只能访问到student节点的成员tag啊，如何访问到student节点本身呢？

现在的问题是<font color="red">如何通过结构的成员访问到结构体本身</font>？

#### 3. 通过结构体成员访问结构体自身

这就提现了C语言的强大之处了，C语言可以直接操作内存（当然是虚拟内存），所以，只要<font color="red">计算出链表节点(```struct list_elem tag``` )在整个节点(```struct student```)的地址偏移量即可</font>。

![](https://gitee.com/imcgr/image_blog/raw/master/20210613132428.png)



那么怎么应该计算呢？计算偏移量似乎也要一个特定的结构体才行啊？不同的结构体（数据节点）计算偏移量过程并不通用啊？

##### 3.1 计算结构体成员的偏移量

这个时候，就充分体现出C语言中的<font color="red">宏</font>的厉害之处了！

```c
/*
	Desciprtion:
		offset计算某个结构体的成员到该结构体起始地址的偏移量
	Parameters:
		struct_type: 结构体的类型
		member: 结构体成员
	Return: 
		结构体成员到该结构体的偏移量
	使用方法：
		例如有一个struct student结构体，有个成员为tag，计算tag到student的偏移量：offset(struct student, tag)
*/
#define offset(struct_type, member) (int)(&((struct_type*)0)->member)
```

上面就是使用C语言的宏来实现的计算偏移量，例如这里要计算student结构体中tag成员到结构体本身的偏移量：

```c
int offset = offset(struct student, tag);
```

几点说明：

- ```(struct_type*)0```
  - 这里把0地址起始的内容强行转化成一个```struct_type```的结构体，很巧妙。
  - <font color="red">0地址处的数据本来不是```struct_type```类型的，但是我们需要一个```struct_type```类型的结构体，所以我们把0地址处当成一个结构体，**借用**一下，然后计算成员地址的偏移量</font>。并没有改变0地址处的内容。
  - 所以直接强行转成```struct_type```类型的结构体，至于内容如何我们并不关心，我们只关心地址。
- ```(struct_type*)0->member```
- 这里巧妙地使用了宏的特性，用到宏的地方会原封不动地替换
  - 比如调用时```offset(struct student, tag)```，在宏的内部，会直接替换成```(struct student*) 0 -> tag```
- ```(int)(&((struct_type*)0)->member)```
  - <font color="red">这个部分有个**大坑**</font>。
  - 这里把地址，强转成int类型变量，就要注意：
    - int类型占用4个字节的（32位）
    - 如果把代码放在64位的机器上跑（我这里是说这里的代码作为普通的应用程序，不是作为内核代码），地址就是64位
    - 那么强转成int类型会截断，那么到时候再恢复成地址时，就会访问错误内存。
  - 所以，如果你的程序是在64位的操作系统中跑，就要把int改为long或者该为uint64_t

##### 3.2 得到结构体本身的地址

计算出了结构体成员的地址偏移量，然后就可以通过结构体成员来访问到结构体自身啦。

```c
/*
	Description: 
		给出某个结构体的类型、成员，以及需要计算的目标结构体成员地址，得到目标结构体的自身地址
	Parameters:
		struct_type: 结构体类型，比如struct student
		struct_menber: 结构体成员名称（这里是变量的名称）
		elem_ptr: 目标结构体成员 地址
	使用方法：
		比如已经得到结构体student1的成员tag的地址tag1_ptr了，那么需要计算student1结构体的地址：
			struct student* s = elem2entry(struct student, tag, tag1_ptr);
*/
#define elem2entry(struct_type, struct_member_name, elem_ptr) \
	 (struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))
```

比如，要计算student1结构体中tag成员的地址tag1_ptr了，计算student1结构体的起始地址：

```c
struct list_elem* tag1_ptr = .....;// 得到了结构体成员tag的地址
struct student* target_student = elem2entry(struct student, tag, tag1_ptr);
```

简单说明：

- 根据传进去的结构体成员tag的地址```tag1_ptr```，然后减去```struct student```类型的结构体中的成员地址偏移量，就可以算出来<font color='dodgerblue'>```tag1_ptr```所指向的的结构体成员</font>的<font color='red'>结构体起始地址</font>f了。
- 所以根据成员的地址tag1_ptr的值，然后减去偏移量offset，就可以得到结构体的起始地址了。

![](https://gitee.com/imcgr/image_blog/raw/master/20210613132618.png)

#### 4. 组成链表

解决了如何根据结构体成员取出结构体，那么剩下的问题就非常容易了。

形成链表，就是把所有的链表节点串起来即可。由于链表节点是通用的（是包含在数据节点中），所以对链表的操作，也都是通用的。

这里说一下，我这里使用的是有头和尾节点的双向链表，也就是结构如下所示：

<img src="https://gitee.com/imcgr/image_blog/raw/master/20210613135216.png" style="zoom: 80%;" />



主要就是对链表增删改查了：

```c
#define offset(struct_type,member) (int)(&((struct_type*)0)->member)
#define elem2entry(struct_type, struct_member_name, elem_ptr) \
	 (struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))

/**********   定义链表结点成员结构   ***********
*结点中不需要数据成元,只要求前驱和后继结点指针*/
struct list_elem {
   struct list_elem* prev; // 前躯结点
   struct list_elem* next; // 后继结点
};

/* 链表结构,用来实现队列 */
struct list {
/* head是队首,是固定不变的，不是第1个元素,第1个元素为head.next */
   struct list_elem head;
/* tail是队尾,同样是固定不变的 */
   struct list_elem tail;
};
/* 初始化双向链表list */
void list_init (struct list* list) {
   list->head.prev = NULL;
   list->head.next = &list->tail;
   list->tail.prev = &list->head;
   list->tail.next = NULL;
}

/* 把链表元素elem插入在元素before之前 */
void list_insert_before(struct list_elem* before, struct list_elem* elem) { 
/* 将before前驱元素的后继元素更新为elem, 暂时使before脱离链表*/ 
   before->prev->next = elem; 

/* 更新elem自己的前驱结点为before的前驱,
 * 更新elem自己的后继结点为before, 于是before又回到链表 */
   elem->prev = before->prev;
   elem->next = before;

/* 更新before的前驱结点为elem */
   before->prev = elem;
}

/* 添加元素到列表队首,类似栈push操作 */
void list_push(struct list* plist, struct list_elem* elem) {
   list_insert_before(plist->head.next, elem); // 在队头插入elem
}

/* 追加元素到链表队尾,类似队列的先进先出操作 */
void list_append(struct list* plist, struct list_elem* elem) {
   list_insert_before(&plist->tail, elem);     // 在队尾的前面插入
}

/* 使元素pelem脱离链表 */
void list_remove(struct list_elem* pelem) {
   pelem->prev->next = pelem->next;
   pelem->next->prev = pelem->prev;
}

/* 将链表第一个元素弹出并返回,类似栈的pop操作 */
struct list_elem* list_pop(struct list* plist) {
   struct list_elem* elem = plist->head.next;
   list_remove(elem);
   return elem;
} 

/* 从链表中查找元素obj_elem,成功时返回true,失败时返回false */
bool elem_find(struct list* plist, struct list_elem* obj_elem) {
   struct list_elem* elem = plist->head.next;
   while (elem != &plist->tail) {
      if (elem == obj_elem) {
	 return true;
      }
      elem = elem->next;
   }
   return false;
}

/* 把列表plist中的每个元素elem和arg传给回调函数func,
 * arg给func用来判断elem是否符合条件.
 * 本函数的功能是遍历列表内所有元素,逐个判断是否有符合条件的元素。
 * 找到符合条件的元素返回元素指针,否则返回NULL. */
struct list_elem* list_traversal(struct list* plist, function func, int arg) {
   struct list_elem* elem = plist->head.next;
/* 如果队列为空,就必然没有符合条件的结点,故直接返回NULL */
   if (list_empty(plist)) { 
      return NULL;
   }

   while (elem != &plist->tail) {
      if (func(elem, arg)) {		  // func返回ture则认为该元素在回调函数中符合条件,命中,故停止继续遍历
	 return elem;
      }					  // 若回调函数func返回true,则继续遍历
      elem = elem->next;	       
   }
   return NULL;
}

/* 返回链表长度 */
uint32_t list_len(struct list* plist) {
   struct list_elem* elem = plist->head.next;
   uint32_t length = 0;
   while (elem != &plist->tail) {
      length++; 
      elem = elem->next;
   }
   return length;
}

/* 判断链表是否为空,空时返回true,否则返回false */
bool list_empty(struct list* plist) {		// 判断队列是否为空
   return (plist->head.next == &plist->tail ? true : false);
}
```













