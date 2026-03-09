#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/sched/signal.h>   // task_struct, children/sibling list
#include <linux/sched.h>          // task_state_to_char
#include <linux/pid.h>            // find_vpid, pid_task

#include <linux/proc_fs.h>        // proc_create, proc_remove
#include <linux/seq_file.h>       // seq_file
#include <linux/rcupdate.h>       // rcu_read_lock

#define PROC_NAME "proc_family"

static int pid = 1;  // default PID (init/systemd), 可按需改
module_param(pid, int, 0444);
MODULE_PARM_DESC(pid, "Target process PID");

static struct proc_dir_entry *proc_entry;

static void print_one_task(struct seq_file *m, const char *role, struct task_struct *t)
{
    if (!t) {
        seq_printf(m, "%s: (null)\n", role);
        return;
    }
    seq_printf(m, "%s: COMM=%-16s PID=%-6d STATE=%c\n",
               role, t->comm, t->pid, task_state_to_char(t));
}

static int proc_family_show(struct seq_file *m, void *v)
{
    struct task_struct *t, *parent, *sib, *child;
    int sib_count = 0, child_count = 0;

    rcu_read_lock();

    /* 通过 PID 找到对应的 task_struct */
    t = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!t) {
        seq_printf(m, "PID %d not found.\n", pid);
        rcu_read_unlock();
        return 0;
    }

    seq_printf(m, "Target PID: %d\n", pid);
    print_one_task(m, "TARGET", t);

    /* 父进程 */
    parent = rcu_dereference(t->real_parent);
    print_one_task(m, "PARENT", parent);

    /* 兄弟进程：同一个父进程的 children 列表里，排除自己 */
    seq_printf(m, "\n[SIBLINGS]\n");
    if (parent) {
        list_for_each_entry(sib, &parent->children, sibling) {
            if (sib == t) continue;
            seq_printf(m, "  COMM=%-16s PID=%-6d STATE=%c\n",
                       sib->comm, sib->pid, task_state_to_char(sib));
            sib_count++;
        }
    }
    if (sib_count == 0)
        seq_printf(m, "  (none)\n");

    /* 子进程：t->children 列表 */
    seq_printf(m, "\n[CHILDREN]\n");
    list_for_each_entry(child, &t->children, sibling) {
        seq_printf(m, "  COMM=%-16s PID=%-6d STATE=%c\n",
                   child->comm, child->pid, task_state_to_char(child));
        child_count++;
    }
    if (child_count == 0)
        seq_printf(m, "  (none)\n");

    rcu_read_unlock();
    return 0;
}

static int proc_family_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_family_show, NULL);
}

static const struct proc_ops proc_family_proc_ops = {
    .proc_open    = proc_family_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init proc_family_init(void)
{
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &proc_family_proc_ops);
    if (!proc_entry) {
        pr_err("proc_family: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }
    pr_info("proc_family: loaded. Use: cat /proc/%s (param pid=%d)\n", PROC_NAME, pid);
    return 0;
}

static void __exit proc_family_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);
    pr_info("proc_family: unloaded.\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Lab Group");
MODULE_DESCRIPTION("Exp1 Q2: List process family (parent/siblings/children) by PID");

module_init(proc_family_init);
module_exit(proc_family_exit);
