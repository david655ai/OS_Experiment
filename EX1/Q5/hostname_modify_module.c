#include <linux/init.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>

#define MOD_NAME "hostname_modify_module"
#define HOSTS_FILE_PATH "/etc/hosts"
#define HOSTNAME_FILE_PATH "/etc/hostname"
#define MAX_TEXT_FILE_SIZE (64 * 1024)

static char hostname_value[__NEW_UTS_LEN + 1] = "";
static char old_hostname[__NEW_UTS_LEN + 1] = "";
static char *old_hosts_content;
static ssize_t old_hosts_len;

module_param_string(new_hostname, hostname_value, sizeof(hostname_value), 0644);
MODULE_PARM_DESC(new_hostname, "新的主机名，长度不能超过 64 个字符");

static int read_text_file(const char *path, char **buffer, ssize_t *length)
{
	struct file *file;
	loff_t pos = 0;
	ssize_t file_size;
	ssize_t read_len;
	char *content;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err(MOD_NAME ": 打开 %s 失败，错误码 %ld\n", path, PTR_ERR(file));
		return PTR_ERR(file);
	}

	file_size = i_size_read(file_inode(file));
	if (file_size < 0 || file_size > MAX_TEXT_FILE_SIZE) {
		filp_close(file, NULL);
		pr_err(MOD_NAME ": %s 大小非法: %zd\n", path, file_size);
		return -EFBIG;
	}

	content = kzalloc(file_size + 1, GFP_KERNEL);
	if (!content) {
		filp_close(file, NULL);
		return -ENOMEM;
	}

	read_len = kernel_read(file, content, file_size, &pos);
	filp_close(file, NULL);
	if (read_len < 0) {
		kfree(content);
		pr_err(MOD_NAME ": 读取 %s 失败，错误码 %zd\n", path, read_len);
		return (int)read_len;
	}

	content[read_len] = '\0';
	*buffer = content;
	*length = read_len;
	return 0;
}

static int write_text_file(const char *path, const char *content, size_t length)
{
	struct file *file;
	loff_t pos = 0;
	ssize_t written_len;

	file = filp_open(path, O_WRONLY | O_TRUNC, 0);
	if (IS_ERR(file)) {
		pr_err(MOD_NAME ": 打开 %s 失败，错误码 %ld\n", path, PTR_ERR(file));
		return PTR_ERR(file);
	}

	written_len = kernel_write(file, content, length, &pos);
	filp_close(file, NULL);
	if (written_len != length) {
		pr_err(MOD_NAME ": 写入 %s 失败，期望 %zu 字节，实际 %zd 字节\n",
		       path, length, written_len);
		return written_len < 0 ? (int)written_len : -EIO;
	}

	return 0;
}

static int write_hostname_file(const char *hostname)
{
	char file_content[__NEW_UTS_LEN + 2];
	ssize_t expected_len;

	expected_len = scnprintf(file_content, sizeof(file_content), "%s\n", hostname);
	return write_text_file(HOSTNAME_FILE_PATH, file_content, expected_len);
}

static int write_hosts_file_with_mapping(const char *hostname)
{
	char mapping_line[128];
	char *new_content;
	size_t mapping_len;
	size_t new_len;
	bool need_newline;

	if (old_hosts_content && strstr(old_hosts_content, hostname))
		return 0;

	mapping_len = scnprintf(mapping_line, sizeof(mapping_line), "127.0.1.1\t%s\n", hostname);
	need_newline = old_hosts_len > 0 && old_hosts_content[old_hosts_len - 1] != '\n';
	new_len = old_hosts_len + (need_newline ? 1 : 0) + mapping_len;

	new_content = kzalloc(new_len + 1, GFP_KERNEL);
	if (!new_content)
		return -ENOMEM;

	if (old_hosts_len > 0)
		memcpy(new_content, old_hosts_content, old_hosts_len);
	if (need_newline)
		new_content[old_hosts_len] = '\n';
	memcpy(new_content + old_hosts_len + (need_newline ? 1 : 0), mapping_line, mapping_len);

	new_content[new_len] = '\0';
	if (write_text_file(HOSTS_FILE_PATH, new_content, new_len)) {
		kfree(new_content);
		return -EIO;
	}

	kfree(new_content);
	return 0;
}

static int __init hostname_modify_module_init(void)
{
	int ret;
	ssize_t copied_len;

	if (hostname_value[0] == '\0') {
		pr_err(MOD_NAME ": 请通过 new_hostname 参数传入新的主机名\n");
		return -EINVAL;
	}

	copied_len = strnlen(hostname_value, sizeof(hostname_value));
	if (copied_len > __NEW_UTS_LEN) {
		pr_err(MOD_NAME ": 主机名长度不能超过 %d 个字符\n", __NEW_UTS_LEN);
		return -EINVAL;
	}

	ret = read_text_file(HOSTS_FILE_PATH, &old_hosts_content, &old_hosts_len);
	if (ret)
		return ret;

	ret = write_hosts_file_with_mapping(hostname_value);
	if (ret)
		goto free_hosts;

	strscpy(old_hostname, utsname()->nodename, sizeof(old_hostname));
	strscpy(utsname()->nodename, hostname_value, sizeof(utsname()->nodename));
	ret = write_hostname_file(hostname_value);
	if (ret) {
		strscpy(utsname()->nodename, old_hostname, sizeof(utsname()->nodename));
		write_text_file(HOSTS_FILE_PATH, old_hosts_content, old_hosts_len);
		goto free_hosts;
	}

	pr_info(MOD_NAME ": 主机名已从 %s 修改为 %s\n", old_hostname, utsname()->nodename);
	return 0;

free_hosts:
	kfree(old_hosts_content);
	old_hosts_content = NULL;
	old_hosts_len = 0;
	return ret;
}

static void __exit hostname_modify_module_exit(void)
{
	if (old_hostname[0] == '\0')
		return;

	strscpy(utsname()->nodename, old_hostname, sizeof(utsname()->nodename));
	if (write_hostname_file(old_hostname))
		pr_warn(MOD_NAME ": 已恢复运行时主机名，但恢复 /etc/hostname 失败\n");
	if (old_hosts_content) {
		if (write_text_file(HOSTS_FILE_PATH, old_hosts_content, old_hosts_len))
			pr_warn(MOD_NAME ": 已恢复主机名，但恢复 /etc/hosts 失败\n");
		kfree(old_hosts_content);
		old_hosts_content = NULL;
		old_hosts_len = 0;
	}

	pr_info(MOD_NAME ": 已恢复原主机名为 %s\n", utsname()->nodename);
}

module_init(hostname_modify_module_init);
module_exit(hostname_modify_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("GitHub Copilot");
MODULE_DESCRIPTION("带参数的主机名修改模块");
