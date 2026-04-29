# 任务一：模拟 Shell（mini_shell）

## 1. 任务目标
实现一个精简版 Shell，能够读取用户输入并执行命令，支持常见的命令解析、重定向、管道、后台执行和环境变量展开。

本项目对应源码文件：`mini_shell.c`

---

## 2. 当前已实现功能

### 2.1 基础命令执行
- 支持执行外部命令（通过 `fork + execvp`），例如：
  - `ls`
  - `cat`
  - `grep`
  - `wc`

### 2.2 内建命令（Builtins）
- `cd [path]`：切换目录
- `pwd`：显示当前工作目录
- `status`：显示上一条命令退出状态码
- `help`：显示帮助信息
- `exit`：退出 shell

### 2.3 重定向
- 输入重定向：`<`
- 输出重定向（覆盖）：`>`
- 输出重定向（追加）：`>>`

### 2.4 管道
- 支持多级管道：
  - `cmd1 | cmd2 | cmd3`

### 2.5 后台执行
- 使用 `&` 将外部命令或管道放到后台执行
- 后台子进程结束后会在提示符前打印完成状态（简化提示）

### 2.6 环境变量展开
- 命令参数中支持 `$VAR` 展开
  - 例如：`echo $HOME`

### 2.7 引号解析
- 支持简单单引号和双引号参数
  - 例如：`echo "hello world"`

### 2.8 信号处理（Ctrl+C）
- Shell 主进程忽略 `Ctrl+C`（`SIGINT`）
- 子进程使用默认信号行为，可被 `Ctrl+C` 中断


---

## 4. 编译与运行

### 4.1 编译
在项目目录下执行：

```bash
make mini_shell
```

### 4.2 运行

```bash
./mini_shell
```

---

## 5. 演示命令（可用于答辩）
按顺序输入以下命令：

```bash
help
pwd
cd ..
pwd

echo hello > out.txt
echo world >> out.txt
cat < out.txt

cat mini_shell.c | grep main | wc -l

echo $HOME
echo "hello world"

sleep 2 &
status

exit
```

---

## 6. 核心实现思路（简述）
1. 主循环使用 `getline` 读取输入。
2. 词法解析支持引号、重定向符、管道符、后台符。
3. 先将输入拆分为 token，再解析为一个或多个 `Command` 结构。
4. 单命令走 `fork/execvp`，多命令走 `pipe/fork/dup2/execvp`。
5. 内建命令在父进程执行，并支持重定向。
6. 每轮循环回收后台子进程，避免僵尸进程。

---

## 7. 注意事项
- 后台模式仅用于外部命令和管道；内建命令不支持 `&`。
- 不支持 `;`、`&&`、`||`，这些会被当作普通字符处理或导致命令失败。
- 不支持 `history/alias/jobs/fg/bg`。

---

## 8. 文件位置
- 源码：`mini_shell.c`
- 本说明：`README_TASK1_CN.md`

---

## 9. 验收建议（按当前版本）

### 9.1 验收前检查清单
1. 在目录下可成功编译：`make mini_shell`。
2. 可执行文件存在且可运行：`./mini_shell`。
3. 不依赖额外第三方库。

### 9.2 现场验收步骤（建议顺序）
1. 启动程序：`./mini_shell`。
2. 演示内建命令：`pwd`、`cd ..`、`pwd`、`status`。
3. 演示重定向：`echo hello > out.txt`、`echo world >> out.txt`、`cat < out.txt`。
4. 演示管道：`cat mini_shell.c | grep main | wc -l`。
5. 演示后台执行：`sleep 2 &`。
6. 演示变量展开：`echo $HOME`。
7. 退出：`exit`。

### 9.3 预期验收现象
1. 命令可被正确解析并执行，提示符持续可用。
2. 重定向结果正确（`out.txt` 内容符合输入）。
3. 管道输出正确。
4. 后台命令可立即返回提示符，并在结束后显示完成提示。
5. `Ctrl+C` 不会终止 shell 主进程。

### 9.4 通过判据（建议口述）
1. 基础命令、重定向、管道、后台执行可稳定演示。
2. 环境变量展开可工作。
3. 程序可正常退出，无明显阻塞问题。
