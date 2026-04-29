#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_FILE "vdisk.img"
#define INPUT_SIZE 1024

static void print_help(void) {
    printf("Commands:\n");
    printf("  my_format\n");
    printf("  my_mkdir <name>\n");
    printf("  my_rmdir <name>\n");
    printf("  my_ls\n");
    printf("  my_cd <name|..|/>\n");
    printf("  my_create <name>\n");
    printf("  my_rm <name>\n");
    printf("  my_open <name>\n");
    printf("  my_close <fd>\n");
    printf("  my_write <fd> <text>\n");
    printf("  my_read <fd> <len>\n");
    printf("  my_exit\n");
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    if (len == 0) {
        return;
    }
    if (s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

int main(void) {
    FileSystem fs;
    if (fs_load(&fs, DISK_FILE) != 0) {
        fs_init_runtime(&fs);
        fs.image.formatted = 0;
    }

    printf("Simple FS (type 'help' for commands)\n");

    char input[INPUT_SIZE];
    while (1) {
        printf("fs> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        trim_newline(input);
        if (strlen(input) == 0) {
            continue;
        }

        char *cmd = strtok(input, " ");
        if (!cmd) {
            continue;
        }

        if (strcmp(cmd, "help") == 0) {
            print_help();
            continue;
        }

        if (strcmp(cmd, "my_exit") == 0) {
            if (fs.image.formatted) {
                if (fs_save(&fs, DISK_FILE) != 0) {
                    printf("Failed to save disk.\n");
                }
            }
            break;
        }

        if (strcmp(cmd, "my_format") == 0) {
            fs_format(&fs);
            printf("Disk formatted.\n");
            continue;
        }

        if (strcmp(cmd, "my_mkdir") == 0) {
            char *name = strtok(NULL, " ");
            if (!name || fs_mkdir(&fs, name) != 0) {
                printf("mkdir failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_rmdir") == 0) {
            char *name = strtok(NULL, " ");
            if (!name || fs_rmdir(&fs, name) != 0) {
                printf("rmdir failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_ls") == 0) {
            if (fs_ls(&fs) != 0) {
                printf("ls failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_cd") == 0) {
            char *name = strtok(NULL, " ");
            if (!name || fs_cd(&fs, name) != 0) {
                printf("cd failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_create") == 0) {
            char *name = strtok(NULL, " ");
            if (!name || fs_create(&fs, name) != 0) {
                printf("create failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_rm") == 0) {
            char *name = strtok(NULL, " ");
            if (!name || fs_rm(&fs, name) != 0) {
                printf("rm failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_open") == 0) {
            char *name = strtok(NULL, " ");
            int fd = -1;
            if (name) {
                fd = fs_open(&fs, name);
            }
            if (fd < 0) {
                printf("open failed.\n");
            } else {
                printf("fd=%d\n", fd);
            }
            continue;
        }

        if (strcmp(cmd, "my_close") == 0) {
            char *fd_str = strtok(NULL, " ");
            if (!fd_str || fs_close(&fs, atoi(fd_str)) != 0) {
                printf("close failed.\n");
            }
            continue;
        }

        if (strcmp(cmd, "my_write") == 0) {
            char *fd_str = strtok(NULL, " ");
            char *text = strtok(NULL, "");
            if (!fd_str || !text) {
                printf("write failed.\n");
                continue;
            }
            int fd = atoi(fd_str);
            int res = fs_write(&fs, fd, (const uint8_t *)text, strlen(text));
            if (res < 0) {
                printf("write failed.\n");
            } else {
                printf("wrote %d bytes.\n", res);
            }
            continue;
        }

        if (strcmp(cmd, "my_read") == 0) {
            char *fd_str = strtok(NULL, " ");
            char *len_str = strtok(NULL, " ");
            if (!fd_str || !len_str) {
                printf("read failed.\n");
                continue;
            }
            int fd = atoi(fd_str);
            int len = atoi(len_str);
            if (len <= 0) {
                printf("read failed.\n");
                continue;
            }
            uint8_t *buf = (uint8_t *)malloc((size_t)len + 1);
            if (!buf) {
                printf("read failed.\n");
                continue;
            }
            size_t out_len = 0;
            int res = fs_read(&fs, fd, buf, (size_t)len, &out_len);
            if (res < 0) {
                printf("read failed.\n");
            } else {
                buf[out_len] = '\0';
                printf("%s\n", buf);
            }
            free(buf);
            continue;
        }

        printf("Unknown command.\n");
    }

    return 0;
}
