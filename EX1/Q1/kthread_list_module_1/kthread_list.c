#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/sched/signal.h>   // for_each_process
#include <linux/sched.h>          // task_struct, task_state_to_char

#include <linux/proc_fs.h>        // proc_create, proc_remove
#include <linux/seq_file.h>       // seq_file helpers
#include <linux/rcupdate.h>       // rcu_read_lock/unlock

#define PROC_NAME "kthread_list"

static struct proc_dir_entry *proc_entry;

/*
 * /proc/kthread_list 的输出函数
 * 列出所有内核线程：comm, pid, state, prio, ppid
 */
static int kthread_list_show(struct seq_file *m, void *v)
{
    struct task_struct *p;

    seq_printf(m, "%-16s %-6s %-6s %-6s %-6s\n",
               "COMM", "PID", "STATE", "PRIO", "PPID");
    seq_printf(m, "--------------------------------------------------\n");

    /*
     * 遍历进程列表时使用 RCU 读锁，避免并发修改导致的问题
     */
    rcu_read_lock();
    for_each_process(p) {
        /* 只保留内核线程 */
        if (!(p->flags & PF_KTHREAD))
            continue;

        seq_printf(m, "%-16s %-6d %-6c %-6d %-6d\n",
                   p->comm,
                   p->pid,
                   task_state_to_char(p),
                   p->prio,
                   p->real_parent ? p->real_parent->pid : 0);
    }
    rcu_read_unlock();

    return 0;
}

static int kthread_list_open(struct inode *inode, struct file *file)
{
    return single_open(file, kthread_list_show, NULL);
}

/* 6.x 内核使用 proc_ops */
static const struct proc_ops kthread_list_proc_ops = {
    .proc_open    = kthread_list_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init kthread_list_init(void)
{
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &kthread_list_proc_ops);
    if (!proc_entry) {
        pr_err("kthread_list: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }
    pr_info("kthread_list: loaded. Use: cat /proc/%s\n", PROC_NAME);
    return 0;
}

static void __exit kthread_list_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);
    pr_info("kthread_list: unloaded.\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Lab Group");
MODULE_DESCRIPTION("Exp1 Q1: List all kernel threads (comm, pid, state, prio, ppid)");

module_init(kthread_list_init);
module_exit(kthread_list_exit);
