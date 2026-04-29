# 实现功能
exit

cd [path]

pwd

status

打印上一条执行命令的「退出状态码」。
状态码含义

0：成功

1-255：失败（不同数字代表不同失败原因）

128+N：被信号 N 杀死（如 Ctrl+C 是信号 2，状态码 130）

help


输入重定向 <

把命令的「标准输入」，从默认的键盘，改成指定的文件。程序不再等待键盘输入，直接从文件里读数据。


输出重定向（覆盖）>

把命令的「标准输出」，从默认的屏幕，改成指定的文件。如果文件不存在则创建，如果存在则覆盖（清空原内容写入）。

输出重定向（追加）>>

和 > 类似，但如果文件存在，在末尾追加内容，而不是覆盖。

管道 |

把前一个命令的「标准输出」，直接作为后一个命令的「标准输入」。像流水线一样串起来，不需要中间文件。


后台执行 &

把命令放到后台执行，Shell 不等待它结束，立即返回提示符，你可以继续输入下一条命令。

环境变量展开 $VAR

把命令里的 $VAR 替换成对应的环境变量值。


单引号 ' ' / 双引号 " "

包裹带空格的参数，让 Shell 把引号里的内容当作一个整体参数，而不是按空格切分成多个参数。

信号处理 Ctrl+C（隐式功能）

Ctrl+C 不会杀死 Mini Shell 主进程，只会被忽略；如果有前台子进程，子进程会收到 SIGINT 信号并终止。

# 代码含义
```C
#define MAX_ARGS 64// 单个命令最多 64 个参数
#define MAX_CMDS 16// 一条管道线最多 16 个命令
#define MAX_LINE 2048// 输入的一行命令最长 2048 字符

typedef struct {
    char *argv[MAX_ARGS];//保存命令和参数，例如：
    int argc;//参数个数
    char *infile;//输入重定向文件指针
    char *outfile;//输出重定向文件
    int append;//标记是否是追加模式。0覆盖，1追加
} Command;//用来保存一条解析好的命令
```

```C
static int g_last_status;//全局变量，保存上一条命令的退出状态码

static void trim_newline(char *s) {//去掉输入行末尾的换行符 \n 或 \r
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
}

static char *trim_spaces(char *s) {//去掉字符串 前面 和 后面 的所有空格，方便解析
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;// 去前面空格
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) *(--end) = '\0';// 去后面空格
    return s;
}

static int status_from_wait(int status) {//把 waitpid 返回的状态，转换成标准 Shell 退出码
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}//这个函数解析子进程的退出状态，正常退出返回退出码，被信号杀死返回 128 + 信号号

static void reap_background_children(void) {//非阻塞回收已经结束的后台子进程，防止僵尸进程
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[done] pid=%ld status=%d\n", (long)pid, status_from_wait(status));
    }
}

static int parse_line(char *line, char *argv[]) {//把一行字符串 → 切成命令 token
    int argc = 0;
    char *p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;//跳过空格
        if (!*p) break;
        if (argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }

        if (*p == '"' || *p == '\'') {//处理引号
            char quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p != quote) {
                fprintf(stderr, "unterminated quote\n");
                return -1;
            }
            *p++ = '\0';
            continue;
        }

        if (*p == '>' || *p == '<' || *p == '|' || *p == '&') {//处理特殊符号
            if (*p == '>' && *(p + 1) == '>') {
                argv[argc++] = ">>";
                p += 2;
            } else {//普通字符串切成参数
                argv[argc++] = (*p == '>') ? ">" : (*p == '<') ? "<" : (*p == '|') ? "|" : "&";
                p++;
            }
            continue;
        }

        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '<' && *p != '|' && *p != '&') p++;
        if (*p) *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;//返回参数个数
}
```

```C
static void expand_environment_variables(int argc, char *argv[]) {//展开环境变量
    for (int i = 0; i < argc; ++i) {//// 如果这个参数以 $ 开头
        if (argv[i][0] == '$' && argv[i][1] != '\0') {
            // 获取环境变量
            const char *value = getenv(argv[i] + 1);
            // 替换成环境变量的值，没有就为空字符串
            argv[i] = (char *)(value ? value : "");
        }
    }
}

static int parse_commands(int argc, char *tokens[], Command cmds[], int *cmd_count) {
   //把切好的 token 数组 → 转成多个 Command 结构体
// 遇到 | 管道
// 结束当前命令
// 新建一个 Command
// 继续解析下一条命令

// 遇到 < 输入重定向
// 把下一个 token 设为 infile

// 遇到 > / >> 输出重定向
// 把下一个 token 设为 outfile

// >> 标记 append = 1

// 其他内容
// 当作普通命令参数存入 argv
   int current = 0;
    if (argc == 0) return -1;
    memset(cmds, 0, sizeof(Command) * MAX_CMDS);

    for (int i = 0; i < argc; ++i) {
        if (strcmp(tokens[i], "|") == 0) {
            if (cmds[current].argc == 0) {
                fprintf(stderr, "invalid pipe usage\n");
                return -1;
            }
            cmds[current].argv[cmds[current].argc] = NULL;
            if (++current >= MAX_CMDS) {
                fprintf(stderr, "too many piped commands\n");
                return -1;
            }
            continue;
        }

        if (strcmp(tokens[i], "<") == 0) {
            if (cmds[current].infile || i + 1 >= argc) {
                +-

                
                fprintf(stderr, "syntax error near '<'\n");
                return -1;
            }
            cmds[current].infile = tokens[++i];
            continue;
        }

        if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            if (cmds[current].outfile || i + 1 >= argc) {
                fprintf(stderr, "syntax error near '>'\n");
                return -1;
            }
            cmds[current].outfile = tokens[++i];
            cmds[current].append = (strcmp(tokens[i - 1], ">>") == 0);
            continue;
        }

        if (strcmp(tokens[i], "&") == 0) {
            fprintf(stderr, "'&' must be at the end\n");
            return -1;
        }

        if (cmds[current].argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }
        cmds[current].argv[cmds[current].argc++] = tokens[i];
    }

    if (cmds[current].argc == 0) {
        fprintf(stderr, "empty command\n");
        return -1;
    }
    cmds[current].argv[cmds[current].argc] = NULL;
    *cmd_count = current + 1;
    return 0;
}

static int apply_redirection(const char *infile, const char *outfile, int append) {//实现重定向
// 输入重定向 <
    if (infile) {
        int fd = open(infile, O_RDONLY);
        if (fd == -1) {
            perror("open input");
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 input");
            close(fd);
            return -1;
        }
        close(fd);
    }
// 输出重定向 <
    if (outfile) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(outfile, flags, 0644);
        if (fd == -1) {
            perror("open output");
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2 output");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

```

```C
static int is_builtin(const char *cmd) {//判断一个命令是不是内建命令
    return strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "status") == 0 || strcmp(cmd, "help") == 0;
}

static int run_builtin(int argc, char *argv[], int *should_exit) {//处理内建命令
    *should_exit = 0;
    if (argc == 0) return 0;

    if (strcmp(argv[0], "exit") == 0) {//设置标志，让 Shell 主循环退出
        *should_exit = 1;
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {//切换工作目录
        const char *path = (argc > 1) ? argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        if (chdir(path) == -1) {
            perror("cd");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[0], "pwd") == 0) {//获取并打印当前目录
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
            return 1;
        }
        puts(cwd);
        return 0;
    }

    if (strcmp(argv[0], "status") == 0) {//打印上一条命令的退出状态
        printf("%d\n", g_last_status);
        return 0;
    }

    if (strcmp(argv[0], "help") == 0) {//打印帮助信息
        puts("builtins: cd pwd status help exit");
        puts("features: quotes, < > >>, pipes, &, $VAR");
        return 0;
    }

    return -1;
}

static int run_builtin_with_redirection(Command *cmd, int *should_exit) {//让内建命令也支持重定向
    int saved_stdin = -1, saved_stdout = -1;

    if (cmd->infile || cmd->outfile) {
        saved_stdin = dup(STDIN_FILENO);//先保存原来的 stdin、stdout
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdin == -1 || saved_stdout == -1) {
            perror("dup");
            if (saved_stdin != -1) close(saved_stdin);
            if (saved_stdout != -1) close(saved_stdout);
            return 1;
        }
        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) {//应用重定向
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
    }

    int result = run_builtin(cmd->argc, cmd->argv, should_exit);

    if (saved_stdin != -1) {
        if (dup2(saved_stdin, STDIN_FILENO) == -1) perror("restore stdin");
        close(saved_stdin);
    }
    if (saved_stdout != -1) {
        if (dup2(saved_stdout, STDOUT_FILENO) == -1) perror("restore stdout");
        close(saved_stdout);
    }
    return result;
}
```

```C
static int run_external(Command *cmd, int background) {//执行外部命令
    int status;
    pid_t pid = fork();//创建子进程
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);// 让Ctrl+C能杀死子进程
        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) _exit(1);// 应用重定向

        execvp(cmd->argv[0], cmd->argv);// 加载外部程序
        perror("execvp");// 如果exec失败，说明命令不存在
        _exit(127);// 退出子进程
    }

    if (background) {//父进程逻辑
        printf("[bg] pid=%ld\n", (long)pid);
        return 0;
    }

    if (waitpid(pid, &status, 0) == -1) {// 前台运行：等待子进程结束
        perror("waitpid");
        return 1;
    }
    return status_from_wait(status);
}

static int run_pipeline(Command cmds[], int cmd_count, int background) {//执行管道命令
    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    int last_status = 0;

    for (int i = 0; i < cmd_count - 1; ++i) {
        if (pipe(pipes[i]) == -1) {//创建管道失败
            perror("pipe");
            for (int j = 0; j < i; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    for (int i = 0; i < cmd_count; ++i) {
        pids[i] = fork();//为每个命令 fork 子进程
        if (pids[i] < 0) {
            perror("fork");//创建失败
            for (int k = 0; k < cmd_count - 1; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            for (int k = 0; k < i; ++k) waitpid(pids[k], NULL, 0);
            return 1;
        }

        if (pids[i] == 0) {
            signal(SIGINT, SIG_DFL);
            if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {// 前一个管道的读端 → 标准输入
                perror("dup2 pipe input");
                _exit(1);
            }
            if (i < cmd_count - 1 && dup2(pipes[i][1], STDOUT_FILENO) == -1) {// 当前管道的写端 → 标准输出
                perror("dup2 pipe output");
                _exit(1);
            }
            for (int k = 0; k < cmd_count - 1; ++k) {//关闭所有管道（非常重要）
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            if (apply_redirection(cmds[i].infile, cmds[i].outfile, cmds[i].append) != 0) _exit(1);//执行重定向 + exec
            execvp(cmds[i].argv[0], cmds[i].argv);
            perror("execvp");
            _exit(127);
        }
    }

    for (int i = 0; i < cmd_count - 1; ++i) {//父进程关闭管道 + 等待
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (background) {
        printf("[bg] pipeline started\n");
        return 0;
    }

    for (int i = 0; i < cmd_count; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");
            last_status = 1;
            continue;
        }
        if (i == cmd_count - 1) last_status = status_from_wait(status);
    }
    return last_status;
}
```

```C
static int execute_line(char *line, int *should_exit) {//总调度函数
//接收一行命令 → 解析 → 判断 → 分发执行（内建 / 外部 / 管道）
    char *argv[MAX_ARGS];
    Command cmds[MAX_CMDS];
    int argc, cmd_count, background = 0;

    *should_exit = 0;
    argc = parse_line(line, argv);//解析命令
    if (argc <= 0) return (argc < 0) ? 1 : 0;

    expand_environment_variables(argc, argv);//展开环境变量

    if (strcmp(argv[argc - 1], "&") == 0) {//判断是否后台运行
        background = 1;
        argv[--argc] = NULL;
    }
    if (argc == 0) return 0;

    if (parse_commands(argc, argv, cmds, &cmd_count) != 0) return 1;//解析为Command结构体

    if (cmd_count == 1 && is_builtin(cmds[0].argv[0])) {//单个内建命令
        if (background) {
            fprintf(stderr, "background mode is not supported for builtin commands\n");
            return 1;
        }
        int code = run_builtin_with_redirection(&cmds[0], should_exit);
        return (code < 0) ? 1 : code;
    }

    if (cmd_count == 1) return run_external(&cmds[0], background);//单个外道命令
    return run_pipeline(cmds, cmd_count, background);//管道命令
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    signal(SIGINT, SIG_IGN);//忽略Ctrl+C

    while (1) {//循环
        int should_exit = 0;
        char *command_text;

        reap_background_children();//先回收僵尸进程

        printf("mini-shell$ ");
        fflush(stdout);

        if (getline(&line, &cap, stdin) == -1) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            perror("getline");
            break;
        }

        trim_newline(line);
        command_text = trim_spaces(line);
        if (command_text[0] == '\0') continue;

        g_last_status = execute_line(command_text, &should_exit);
        if (should_exit) break;
    }

    free(line);
    return 0;
}

```
# fork
```C
pid = fork()
```触发系统调用

内核创建新进程，给子进程返回0，父进程返回子进程PID

```C
SYSCALL_DEFINE0(fork)//系统调用，参数个数为0
{
#ifdef CONFIG_MMU
	return _do_fork(SIGCHLD, 0, 0, NULL, NULL, 0);//参数含义，子进程退出时给父进程发的信号，栈起始位置，标志位，父进程寄存器状态，子进程寄存器状态，自定义tid。子进程退出时会给父进程发送SIGCHLD信号
#else
	/* can not support in nommu mode */
	return -EINVAL;
#endif
}
#endif
```
调用_do_fork

```C
//只做调度，分配，启动，返回
long _do_fork(unsigned long clone_flags,// 克隆标志（决定父子共享哪些资源）子进程退出时发信号给父进程
	      unsigned long stack_start, // 进程栈起始地址
	      unsigned long stack_size,// 栈大小
	      int __user *parent_tidptr,// 父进程TID存放地址
	      int __user *child_tidptr,// 子进程TID存放地址
	      unsigned long tls)// 线程本地存储
{
	struct completion vfork;
	struct pid *pid;// PID 对象
	struct task_struct *p;// 子进程描述符指针
	int trace = 0;
	long nr; // 最终返回的 PID 号

	/*
	 * Determine whether and which event to report to ptracer.  When.
     .
	 * called from kernel_thread or CLONE_UNTRACED is explicitly
	 * requested, no event is reported; otherwise, report if the event
	 * for the type of forking is enabled.
	 */
	if (!(clone_flags & CLONE_UNTRACED)) {
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		else if ((clone_flags & CSIGNAL) != SIGCHLD)
			trace = PTRACE_EVENT_CLONE;
		else
			trace = PTRACE_EVENT_FORK;

		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;
	}//调试用fork不管

	p = copy_process(clone_flags, stack_start, stack_size,
			 child_tidptr, NULL, trace, tls, NUMA_NO_NODE);//把父进程完整复制成一个新子进程
	add_latent_entropy();

	if (IS_ERR(p))
		return PTR_ERR(p);

	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	trace_sched_process_fork(current, p);

	pid = get_task_pid(p, PIDTYPE_PID);//获取子进程PID
	nr = pid_vnr(pid);//转化为用户态可见的PID

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, parent_tidptr);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		init_completion(&vfork);
		get_task_struct(p);
	}

	wake_up_new_task(p);//唤醒子进程，将子进程加入就绪队列

	/* forking complete and child started to run, tell ptracer */
	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork))
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);//子进程返回0
	return nr;//返回PID子进程
}
```
```C
copy_process
1.检查参数合法性，各种flag组合
2.复制进程描述符，创建一个task_struct p = dup_task_struct(current);
3.初始化权限、信号、锁、调度信息
4.检查进程数是否超限，防止fork无限复制进程
5.复制各种资源
copy_files();      // 复制文件描述符
copy_fs();         // 复制当前目录、根目录
copy_sighand();    // 复制信号处理函数
copy_mm();         // 复制内存空间（写时复制）
copy_namespaces(); // 复制命名空间
copy_thread_tls(); // 复制线程上下文
6.分配PID
pid = alloc_pid(...)
7.建立父子关系
p->real_parent = current;
list_add_tail(&p->sibling, &current->children);
```

# execvp
替换进程，换成对应的命令程序
```C
execvp(文件名, 参数数组);//第一个参数：要运行的程序名，第二个参数，命令参数数组
```
execvp成功直接退出，失败才会打印错误，把当前的程序替换成新程序