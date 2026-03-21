# 任务一：模拟 Shell（mini_shell）

## 1. 任务目标
实现一个简化版 Shell，能够读取用户输入并执行命令，支持常见的命令解析、重定向、管道、后台任务和历史功能。

本项目对应源码文件：`mini_shell.c`

---

## 2. 已实现功能

### 2.1 基础命令执行
- 支持执行外部命令（通过 `fork + execvp`），例如：
  - `ls`
  - `cat`
  - `grep`
  - `wc`

### 2.2 内建命令（Builtins）
- `cd [path]`：切换目录
- `pwd`：显示当前工作目录
- `exit`：退出 shell
- `help`：显示功能帮助
- `history`：显示历史命令
- `status`：显示上一条命令退出状态码
- `export NAME=value`：设置环境变量
- `alias`：查看或设置别名
- `jobs`：查看后台任务
- `fg <job-id>`：将后台任务切换到前台
- `bg <job-id>`：让任务在后台继续运行（发送 `SIGCONT`）

### 2.3 重定向
- 输入重定向：`<`
- 输出重定向（覆盖）：`>`
- 输出重定向（追加）：`>>`

### 2.4 管道
- 支持多级管道：
  - `cmd1 | cmd2 | cmd3`

### 2.5 命令连接控制
- 顺序执行：`;`
- 条件与：`&&`
- 条件或：`||`

### 2.6 后台任务
- 使用 `&` 将命令放到后台执行
- 支持配套的 `jobs / fg / bg` 管理

### 2.7 历史增强
- `!!`：执行上一条历史命令
- `!n`：执行第 n 条历史命令（如 `!2`）
- `!prefix`：执行最近一条以前缀匹配的历史命令（如 `!ec`）

### 2.8 环境变量展开
- 命令参数中支持 `$VAR` 展开
  - 例如：`echo $NAME`

### 2.9 信号处理（Ctrl+C）
- `Ctrl+C` 不会终止 Shell 主进程
- 若有前台任务，`Ctrl+C` 会转发到前台进程组，用于中断前台命令

### 2.10 启动配置加载
- 启动时自动读取配置文件：
  1. 当前目录 `.minishellrc`
  2. `$HOME/.minishellrc`（若存在）
- 支持在配置文件中预置 `alias`、`export` 等命令

---

## 3. 编译与运行

### 3.1 编译
在项目目录下执行：

```bash
make mini_shell
```

### 3.2 运行

```bash
./mini_shell
```

---

## 4. 演示命令（可用于答辩）
按顺序输入以下命令：

```bash
help
export NAME=david
echo $NAME
alias ll="ls -1"
ll | wc -l

echo one; echo two
false && echo no
false || echo yes

echo hello > out.txt
echo world >> out.txt
cat < out.txt

sleep 3 &
jobs
fg 1
status

history
!!
!2
!ec

exit
```

---

## 5. `.minishellrc` 示例
可在当前目录创建：

```bash
alias hi="echo RC_OK"
export RC_NAME=from_rc
```

启动 Shell 后可直接验证：

```bash
hi
echo $RC_NAME
```

---

## 6. 核心实现思路（简述）
1. 主循环使用 `getline` 读取输入。
2. 词法解析支持引号、重定向符、管道符、后台符。
3. 先按 `; && ||` 切分执行段，再逐段执行。
4. 每段再按管道拆分为多个命令，完成 `pipe/fork/dup2/execvp`。
5. 对内建命令在父进程执行，并支持重定向。
6. 维护历史表、别名表、作业表，完成 `history/alias/jobs` 管理。
7. 使用进程组和信号转发实现 `Ctrl+C` 对前台任务的控制。

---

## 7. 注意事项
- 后台模式仅用于外部命令；内建命令不支持 `&`。
- `fg/bg` 需传入有效任务编号，如 `fg 1`。
- 历史命令未命中时会提示 `history event not found`。

---

## 8. 文件位置
- 源码：`mini_shell.c`
- 本说明：`README_TASK1_CN.md`

---

## 9. 详细验收细节

### 9.1 验收前检查清单
1. 在干净目录下可成功编译：`make mini_shell`。
2. 执行文件存在且可运行：`./mini_shell`。
3. 不依赖额外第三方库或手动环境变量。
4. `.minishellrc` 演示前后状态可控（是否存在你要心里有数）。

### 9.2 现场验收步骤（建议顺序）
1. 启动程序：`./mini_shell`。
2. 演示内建命令：`pwd`、`cd ..`、`pwd`。
3. 演示重定向：`echo hello > out.txt`、`echo world >> out.txt`、`cat < out.txt`。
4. 演示管道：`cat mini_shell.c | grep main | wc -l`。
5. 演示命令控制：`false && echo no`、`false || echo yes`、`echo a; echo b`。
6. 演示后台任务：`sleep 2 &`、`jobs`、`fg 1`。
7. 演示历史增强：`history`、`!!`、`!2`、`!ec`。
8. 演示环境变量：`export NAME=david`、`echo $NAME`。
9. 退出：`exit`。

### 9.3 预期验收现象
1. 命令能被正确解析执行，提示符持续可用。
2. 重定向结果正确（`out.txt` 内容符合输入）。
3. 管道输出可见且符合组合命令逻辑。
4. `&& / || / ;` 的条件执行符合预期。
5. 后台任务可列出、可前台恢复。
6. 历史命令可按 `!! / !n / !prefix` 重放。
7. `Ctrl+C` 不会终止 shell 主进程（仅中断前台任务）。

### 9.4 通过判据（建议口述）
1. 基础命令、重定向、管道、控制符、后台任务全部可演示。
2. 历史增强和环境变量扩展可稳定工作。
3. 程序退出后无明显残留副作用（如卡死、僵尸前台逻辑）。

### 9.5 常见扣分点
1. 只支持简单命令，不支持重定向/管道。
2. 后台任务不可管理（只有 `&` 没有 `jobs/fg/bg`）。
3. `Ctrl+C` 直接把 shell 主进程打掉。
4. 历史命令异常（`!!` 能用但 `!n`、`!prefix` 失败）。
5. 仅“能跑一次”，重复操作后出现阻塞或状态错误。
