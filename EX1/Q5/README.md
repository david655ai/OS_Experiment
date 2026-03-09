# 带参数的主机名修改模块

这个题目要求实现一个**带参数的 Linux 内核模块**，参数为新的主机名，模块加载后把系统原主机名修改成传入的字符串，并同步写入 `/etc/hostname` 与 `/etc/hosts`。

## 文件说明

- `hostname_modify_module.c`：模块源代码
- `Makefile`：模块编译文件

## 设计说明

- 模块参数名：`new_hostname`
- 参数作用：接收新的主机名
- 关键思路：参考 `/etc/hostname` 中保存的主机名内容，模块加载时把内核中的 `nodename` 改成参数值，同时把新主机名写回 `/etc/hostname`
- 为了避免 `sudo` 出现“无法解析主机”的提示，模块加载时会为新主机名补充 `/etc/hosts` 中的 `127.0.1.1` 映射，卸载时恢复原始内容
- 兼容性说明：当前实现只使用模块可访问的公开接口，避免依赖未导出的内核符号
- 为了便于演示，模块卸载时会自动恢复原主机名

## 编译

```bash
make
```

编译成功后会生成 `hostname_modify_module.ko`。

## 加载模块并修改主机名

```bash
sudo insmod hostname_modify_module.ko new_hostname=llkk
hostname
cat /etc/hostname
```

说明：

- `hostname` 命令会显示当前运行内核中的主机名
- `/etc/hostname` 是主机名配置文件，当前实现会同步改写该文件内容
- `/etc/hosts` 会补充新主机名映射，因此正常情况下不会再出现 `sudo` 的主机名解析告警

## 卸载模块并恢复原主机名

```bash
sudo rmmod hostname_modify_module
hostname
```

说明：

- 卸载模块后，运行时主机名、`/etc/hostname` 和 `/etc/hosts` 都会恢复到加载模块前的内容

## 代码核心

模块在初始化函数中：

1. 读取模块参数 `new_hostname`
2. 保存原主机名与原始 `/etc/hosts` 内容
3. 为新主机名补充 `/etc/hosts` 映射
4. 修改 `utsname()->nodename`
5. 将新主机名写入 `/etc/hostname`

模块退出函数中：

1. 将主机名恢复为原值
2. 将 `/etc/hostname` 恢复为原值
3. 将 `/etc/hosts` 恢复为原值

## 注意事项

- 新主机名长度不能超过 64 个字符
- 加载/卸载模块通常需要 root 权限
- 当前实现会直接修改系统文件，适合实验环境，不建议在生产环境中通过内核模块操作 `/etc/hosts`
- 该实现适合课程实验与原理演示
