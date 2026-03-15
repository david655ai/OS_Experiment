# 进程资源使用情况查询模块

### 题目说明
本题要求实现一个带参数的 Linux 内核模块，模块参数为进程 ID（PID）。  
模块加载后在内核中查找该进程，并输出该进程的资源使用情况，包括用户态执行时间、内核态执行时间、页面错误次数以及进程被换出内存的次数。  
相关信息通过内核日志（dmesg）进行查看。

### 文件说明
process_usage_module.c：模块源代码  
Makefile：模块编译文件  

### 设计说明
模块参数名：pid  

参数作用：接收需要查询的进程 ID。

模块加载时根据输入的 PID 在内核中查找对应的进程，并读取该进程的资源使用信息。  
Linux 内核中每个进程的信息保存在 task_struct 结构体中，该结构体相当于进程控制块（PCB），记录了进程运行过程中的各种统计数据。

模块主要读取 task_struct 中的以下字段：

utime：用户态 CPU 执行时间  
stime：内核态 CPU 执行时间  
min_flt：次要页面错误次数（无需磁盘 I/O）  
maj_flt：主要页面错误次数（需要磁盘 I/O）  
nvcsw 和 nivcsw：上下文切换次数（作为进程换出次数的近似统计）

由于内核中的时间通常以纳秒为单位存储，因此模块会将时间转换为“秒 + 微秒”的形式进行输出。

### 编译
在当前目录执行：

make

编译成功后会生成 process_usage_module.ko 文件。

### 加载模块并查询进程资源
sudo insmod process_usage_module.ko pid=1234

其中 1234 为需要查询的进程 ID。

### 查看输出
dmesg | tail

示例输出：

Process PID: 1234  
User Time: 5 sec 123456 usec  
System Time: 1 sec 654321 usec  
Minor Page Faults: 120  
Major Page Faults: 3  
Swap Count: 0  

### 卸载模块
sudo rmmod process_usage_module

### 注意事项
1. 输入的 PID 必须是系统中存在的进程，否则模块会提示找不到该进程。  
2. 加载和卸载内核模块通常需要 root 权限。  
3. 模块只读取进程资源信息，不会对系统状态产生影响。  
4. 输出信息通过 printk 写入内核日志，需要使用 dmesg 查看。  
5. 本实现主要用于操作系统课程实验与 Linux 内核模块原理学习。