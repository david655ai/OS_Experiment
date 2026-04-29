#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>

#define FS_MAGIC 0x46533130u /* "FS10" */

#define BLOCK_SIZE 1024
#define BLOCK_COUNT 1024
#define DISK_DATA_SIZE (BLOCK_SIZE * BLOCK_COUNT)

#define MAX_INODES 256
#define MAX_NAME 32
#define MAX_DIR_CHILDREN 64
#define MAX_OPEN_FILES 32

#define FAT_FREE (-1)
#define FAT_EOC (-2)

typedef struct {
    int used;
    int is_dir;
    int parent;
    int size;
    int first_block;
    char name[MAX_NAME];
    int child_count;
    int children[MAX_DIR_CHILDREN];
} Inode;

typedef struct {
    int used;
    int inode_index;
    int offset;
} OpenFile;

typedef struct {
    uint32_t magic;
    int formatted;
    int block_size;
    int block_count;
    int fat[BLOCK_COUNT];
    Inode inodes[MAX_INODES];
    uint8_t data[DISK_DATA_SIZE];
} FSImage;

typedef struct {
    FSImage image;
    int current_dir;
    OpenFile open_files[MAX_OPEN_FILES];
} FileSystem;

int fs_load(FileSystem *fs, const char *path);
int fs_save(FileSystem *fs, const char *path);
int fs_format(FileSystem *fs);

int fs_mkdir(FileSystem *fs, const char *name);
int fs_rmdir(FileSystem *fs, const char *name);
int fs_ls(FileSystem *fs);
int fs_cd(FileSystem *fs, const char *name);

int fs_create(FileSystem *fs, const char *name);
int fs_rm(FileSystem *fs, const char *name);

int fs_open(FileSystem *fs, const char *name);
int fs_close(FileSystem *fs, int fd);
int fs_write(FileSystem *fs, int fd, const uint8_t *buf, size_t len);
int fs_read(FileSystem *fs, int fd, uint8_t *buf, size_t len, size_t *out_len);

void fs_init_runtime(FileSystem *fs);

#endif
