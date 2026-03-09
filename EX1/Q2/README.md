# file_copy_module

这是一个带参数的 Linux 内核模块，参数为：

- `src`：源文件路径
- `dst`：目标文件路径

模块在加载时执行文件拷贝。

## 编译

```bash
make
```

## 加载示例

```bash
sudo insmod file_copy_module.ko src="/home/myos/source.txt" dst="/home/myos/target.txt"
```

## 查看日志

```bash
dmesg | tail -20
```

## 卸载模块

```bash
sudo rmmod file_copy_module
```

## 说明

- 目标文件不存在时会自动创建。
- 目标文件已存在时会被覆盖。
- 这是教学实验示例，实际生产环境一般不建议在内核模块中直接做文件拷贝。
