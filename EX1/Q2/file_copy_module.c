#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#define COPY_BUF_SIZE 4096

// 用于接收用户传入的源文件路径和目标文件路径
static char *src = NULL;
static char *dst = NULL;

// owner：读写；group：读；other：读
module_param(src, charp, 0644);
MODULE_PARM_DESC(src, "Source file path");

module_param(dst, charp, 0644);
MODULE_PARM_DESC(dst, "Destination file path");

static int copy_file_in_kernel(const char *src_path, const char *dst_path)
{
	struct file *src_file;
	struct file *dst_file;
	char *buffer;
	loff_t src_pos = 0;
	loff_t dst_pos = 0;
	ssize_t bytes_read;
	ssize_t bytes_written;
	int ret = 0;

	// GFP_KERNEL表示允许内核在内存不足时休眠等待
	buffer = kmalloc(COPY_BUF_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM; // Error NO MEMory

	// 以只读模式打开源文件
	src_file = filp_open(src_path, O_RDONLY, 0);
	if (IS_ERR(src_file)) {
		ret = PTR_ERR(src_file);
		pr_err("copy_module: failed to open source file %s, error=%d\n",
		       src_path, ret);
		goto out_free;
	}

	// 以写入模式打开目标文件
	dst_file = filp_open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(dst_file)) {
		ret = PTR_ERR(dst_file);
		pr_err("copy_module: failed to open destination file %s, error=%d\n",
		       dst_path, ret);
		goto out_close_src;
	}

	// kernel_read返回读到的字节数
	while ((bytes_read = kernel_read(src_file, buffer, COPY_BUF_SIZE,
					 &src_pos)) > 0) {
		bytes_written = kernel_write(dst_file, buffer, bytes_read, &dst_pos);
		
		// 写入失败
		if (bytes_written < 0) {
			ret = bytes_written;
			pr_err("copy_module: write failed, error=%d\n", ret);
			goto out_close_both;
		}

		// 写入的不等于读取的，发生了部分写入错误
		if (bytes_written != bytes_read) {
			ret = -EIO;
			pr_err("copy_module: partial write, read=%zd, written=%zd\n",
			       bytes_read, bytes_written);
			goto out_close_both;
		}
	}

	if (bytes_read < 0) {
		ret = bytes_read;
		pr_err("copy_module: read failed, error=%d\n", ret);
		goto out_close_both;
	}

	pr_info("copy_module: copied from %s to %s successfully\n",
		src_path, dst_path);

out_close_both:
	filp_close(dst_file, NULL);
out_close_src:
	filp_close(src_file, NULL);
out_free:
	kfree(buffer);
	return ret;
}

// 模块初始化函数，检查路径是否正确，并调用文件复制函数
static int __init copy_module_init(void)
{
	if (!src || !dst) {
		pr_err("copy_module: src and dst parameters are required\n");
		return -EINVAL;
	}

	pr_info("copy_module: loading with src=%s dst=%s\n", src, dst);
	return copy_file_in_kernel(src, dst);
}

static void __exit copy_module_exit(void)
{
	pr_info("copy_module: unloaded\n");
}

// 定义模块的初始化和退出函数
module_init(copy_module_init);
module_exit(copy_module_exit);

MODULE_LICENSE("GPL");