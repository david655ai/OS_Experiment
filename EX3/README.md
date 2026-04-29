# EX3 Linux IPC and Shell

本目录包含四个独立的 C 程序（任务一至任务四）：

1. mini_shell.c: 迷你 Shell（命令解析、管道、重定向、后台执行）
2. pipe_ipc.c: 父子进程管道通信（双管道请求-响应）
3. msgqueue_threads.c: POSIX 消息队列的线程间通信（优先级与超时）
4. shm_processes.c: 共享内存 + 信号量的进程间通信（请求-响应）

## English Overview (Simple Function Summary)

- mini_shell.c: A minimal shell that parses commands and runs builtins or external programs with pipes, redirection, background jobs, and basic variable expansion.
- pipe_ipc.c: A parent-child IPC demo using two anonymous pipes to implement a request-response workflow.
- msgqueue_threads.c: Two threads communicate through POSIX message queues with priorities, timeouts, and RTT statistics.
- shm_processes.c: Two processes exchange requests and responses via shared memory synchronized by named semaphores, with timing stats and cleanup.

## 任务说明与详细 Readme

- 任务一（mini shell）：README_TASK1_CN.md
- 任务二（pipe IPC）：README_TASK2_CN.md
- 任务三（message queue）：README_TASK3_CN.md
- 任务四（shared memory）：README_TASK4_CN.md

## 一键编译

```bash
make
```

## 单独编译与运行

```bash
make mini_shell && ./mini_shell
make pipe_ipc && ./pipe_ipc
make msgqueue_threads && ./msgqueue_threads
make shm_processes && ./shm_processes
```

## mini_shell 简要功能

- 内建命令：cd, pwd, exit, history, status, help, export, alias, jobs, fg, bg
- 外部命令：fork + execvp
- 重定向：<, >, >>
- 管道：多级管道
- 后台执行：&
- 历史扩展：!!, !n, !prefix
- 变量展开：$VAR
- 信号处理：Ctrl+C 不终止 shell 本体，转发给前台任务

## mini_shell 示例

```bash
help
export NAME=david
echo $NAME
alias ll="ls -1"
ll | wc -l
pwd
echo hello > out.txt
echo world >> out.txt
cat < out.txt
pwd > pwd.txt
cat mini_shell.c | grep static | wc -l
echo "hello world"
sleep 1 &
jobs
fg 1
history
!!
!2
!ec
status
exit
```

## .minishellrc

启动时会加载 `.minishellrc`（当前目录优先，其次 `$HOME/.minishellrc`）。

示例：

```bash
alias ll="ls -1"
export NAME=david
```

## 清理

```bash
make clean
```
