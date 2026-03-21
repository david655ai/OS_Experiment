# EX3 Linux IPC and Shell

This folder contains four standalone C programs:

1. mini_shell.c: a simple shell simulator
2. pipe_ipc.c: parent-child communication via pipe
3. msgqueue_threads.c: two-thread communication via POSIX message queue
4. shm_processes.c: two-process communication via POSIX shared memory and semaphore

## Build

Run:

make

## Run

Run each program:

./mini_shell
./pipe_ipc
./msgqueue_threads
./shm_processes

## mini_shell examples

Inside mini shell:

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
echo one; echo two
false || echo fallback
sleep 1 &
jobs
fg 1
history
!!
!2
!ec
status
exit

## .minishellrc

At startup, mini shell loads commands from `.minishellrc` (current directory first, then `$HOME/.minishellrc` if present).

Example `.minishellrc`:

alias ll="ls -1"
export NAME=david

## mini_shell capabilities

- Builtins: cd, pwd, exit, history, status, help, export, alias, jobs, fg, bg
- External command execution via fork + execvp
- Redirection: <, >, >>
- Pipes: multi-stage pipelines
- Command chaining: ;, &&, ||
- Background jobs: & with jobs/fg/bg
- History expansion: !!, !n, !prefix
- Environment variable expansion: $VAR
- Signal handling: Ctrl+C does not terminate mini shell; it is forwarded to the foreground job

## Clean

make clean
