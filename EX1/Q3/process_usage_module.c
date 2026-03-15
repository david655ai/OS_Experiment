#include <linux/module.h>    // 所有模块都需要包含的头文件
#include <linux/kernel.h>    // 包含常用的系统日志打印函数 printk
#include <linux/init.h>      // 包含模块初始化和清理的宏定义
#include <linux/sched.h>     // 包含进程描述符 task_struct 的定义
#include <linux/pid.h>       // 包含 PID 处理相关的函数
#include <linux/sched/signal.h> // 包含进程资源统计相关的宏

// 模块元数据声明
MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS_Student");
MODULE_DESCRIPTION("A module to report process resource usage by PID");

// 接收命令行参数：进程 PID
static int pid = 1; 
module_param(pid, int, 0644);
MODULE_PARM_DESC(pid, "The PID of the process to monitor");

// 转换纳秒为秒和微秒的辅助宏
#define NSEC_PER_SEC  1000000000L
#define NSEC_PER_USEC 1000L

static int __init process_usage_init(void)
{
    struct task_struct *task;
    struct pid *pid_struct;
    unsigned long long user_ns, sys_ns;

    printk(KERN_INFO "--- Process Usage Module Loaded ---\n");

    // 1. 根据传入的 PID 获取 pid 结构体
    pid_struct = find_get_pid(pid);
    if (!pid_struct) {
        printk(KERN_ERR "Invalid PID: %d\n", pid);
        return -EINVAL;
    }

    // 2. 根据 pid 结构体获取进程的 task_struct
    task = get_pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        printk(KERN_ERR "Could not find task for PID: %d\n", pid);
        put_pid(pid_struct);
        return -EINVAL;
    }

    // 3. 读取资源使用信息
    // 这里的 utime 和 stime 在内核中以纳秒(ns)为单位存储
    user_ns = ktime_to_ns(task->utime);
    sys_ns  = ktime_to_ns(task->stime);

    // 4. 输出格式化信息到内核日志
    printk(KERN_INFO "Process PID: %d\n", pid);
    
    // 输出用户态和内核态执行时间
    printk(KERN_INFO "User Time: %llu sec %llu usec\n", 
           user_ns / NSEC_PER_SEC, (user_ns % NSEC_PER_SEC) / NSEC_PER_USEC);
    printk(KERN_INFO "System Time: %llu sec %llu usec\n", 
           sys_ns / NSEC_PER_SEC, (sys_ns % NSEC_PER_SEC) / NSEC_PER_USEC);

    // 输出缺页中断和换出次数 (从 task->rss_stat 或 task 结构体相关字段获取)
    // min_flt: 次要缺页, maj_flt: 主要缺页, nvcsw+nivcsw: 上下文切换(此处视为swap近似参考)
    printk(KERN_INFO "Minor Page Faults: %lu\n", task->min_flt);
    printk(KERN_INFO "Major Page Faults: %lu\n", task->maj_flt);
    printk(KERN_INFO "Swap Count: %lu\n", task->nvcsw + task->nivcsw);

    // 5. 释放引用计数
    put_task_struct(task);
    put_pid(pid_struct);

    return 0;
}

static void __exit process_usage_exit(void)
{
    printk(KERN_INFO "--- Process Usage Module Unloaded ---\n");
}

module_init(process_usage_init);
module_exit(process_usage_exit);