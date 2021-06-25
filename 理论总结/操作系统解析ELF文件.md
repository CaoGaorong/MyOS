## 操作系统解析ELF文件

### 一、ELF 文件格式介绍

ELF文件可以看成以下几个部分：

- ELF Header
- Program Header Table
- Section Header Table
- data

ELF文件结构如下图：

<img src="https://i.loli.net/2021/05/26/jlvsuaKqr2ewtT8.jpg" style="zoom: 67%;" />

ELF Header和 program header、section header关系：

- 也就是ELF文件中的指令和数据是分段的，分成了很多segment
- 每一个段segment都有program header来描述，很多段就有很多program header组成了program header table
- 每个一段segment 又可以分成很多节section
- 每一个节section 也有一个section header来描述，很多section header 组成section header table

 

关于program header和section header:

- 一个segment（program header指向的） 包含多个section（section header指向的）
- program header 和 section header指向的是同一块物理空间

所以要想定位到ELF文件中的指令和数据（只需定位segment即可），要先根据ELF header定位 program header，再根据program header找到各个段segment的位置。



### 二、ELF Header 和 Program Header

ELF Header的字段和 Program Header的字段，如下图所示：

![](https://i.loli.net/2021/05/26/fbTsJ1QXcA6ajEr.png)

ELF Header中，有颜色的字段，可以找到 program header的位置：

- ```e_phnum```表示program header的个数
- ```e_phentsize```表示program header的大小（每个Program header大小一样）
- ```e_phoff```表示第一个program header的位置
  - ```e_phoff ```+ ```e_phentsize```就是第二个Program header 的位置



根据Program Header，可以找到对应的segment：

- ```p_offset```表示段segment所在文件的偏移
- ```p_filesz```表示一个segment的大小
- ```p_vaddr```表示一个segment加载到内存的虚拟地址



> program header是segment的一部分

### 三、操作系统如何解析ELF文件

所谓的执行ELF文件，就是取出ELF文件中的指令和数据，然后执行，也就是取出文件里面的段segment。

要想取出ELF文件里面的段segment，基本步骤如下：

- 根据ELF Header定位Program Header
  - 根据```e_phoff```找到program header table，也就是第一个program header的起始地址
  - 从program header table 取出program header，根据```e_phnum```可以知道，表中有几个program header，要循环几次
  - 根据```e_phentsize```知道program header大小，配合起始地址```e_phoff```，就可以知道一个program header的终止地址

- 根据Program Header定位段Segment
  - 根据```p_offset```和```p_filesz```知道了segment的起始和终止地址
  - 根据```p_vaddr```知道了这个segment要加载到哪个内存中去

操作系统根据上述操作，把ELF文件中的segment取出来，然后加载到内存中某个位置，就可以执行了。

### 四、参考

关于program header和section header https://medium.com/@holdengrissett/linux-101-understanding-the-insides-of-your-program-2be2480ba366

