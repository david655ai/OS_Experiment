#include "fs.h"

#include <stdio.h>
#include <string.h>

static int find_child(FileSystem *fs, int dir_index, const char *name) {
    Inode *dir = &fs->image.inodes[dir_index];
    for (int i = 0; i < dir->child_count; i++) {
        int child_idx = dir->children[i];
        Inode *child = &fs->image.inodes[child_idx];
        if (child->used && strncmp(child->name, name, MAX_NAME) == 0) {
            return child_idx;
        }
    }
    return -1;
}

static int allocate_inode(FileSystem *fs, const char *name, int is_dir, int parent) {
    for (int i = 0; i < MAX_INODES; i++) {
        Inode *node = &fs->image.inodes[i];
        if (!node->used) {
            node->used = 1;
            node->is_dir = is_dir;
            node->parent = parent;
            node->size = 0;
            node->first_block = FAT_EOC;
            node->child_count = 0;
            memset(node->children, 0, sizeof(node->children));
            memset(node->name, 0, sizeof(node->name));
            strncpy(node->name, name, MAX_NAME - 1);
            return i;
        }
    }
    return -1;
}

static int add_child(FileSystem *fs, int dir_index, int child_index) {
    Inode *dir = &fs->image.inodes[dir_index];
    if (dir->child_count >= MAX_DIR_CHILDREN) {
        return -1;
    }
    dir->children[dir->child_count++] = child_index;
    return 0;
}

static int remove_child(FileSystem *fs, int dir_index, int child_index) {
    Inode *dir = &fs->image.inodes[dir_index];
    for (int i = 0; i < dir->child_count; i++) {
        if (dir->children[i] == child_index) {
            for (int j = i; j < dir->child_count - 1; j++) {
                dir->children[j] = dir->children[j + 1];
            }
            dir->children[dir->child_count - 1] = 0;
            dir->child_count--;
            return 0;
        }
    }
    return -1;
}

static int allocate_block(FileSystem *fs) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (fs->image.fat[i] == FAT_FREE) {
            fs->image.fat[i] = FAT_EOC;
            memset(&fs->image.data[i * BLOCK_SIZE], 0, BLOCK_SIZE);
            return i;
        }
    }
    return -1;
}

static void free_chain(FileSystem *fs, int start_block) {
    int cur = start_block;
    while (cur >= 0 && cur < BLOCK_COUNT) {
        int next = fs->image.fat[cur];
        fs->image.fat[cur] = FAT_FREE;
        if (next == FAT_EOC) {
            break;
        }
        cur = next;
    }
}

static int get_block_at(FileSystem *fs, Inode *node, int block_offset, int allocate) {
    if (node->first_block == FAT_EOC) {
        if (!allocate) {
            return -1;
        }
        int new_block = allocate_block(fs);
        if (new_block < 0) {
            return -1;
        }
        node->first_block = new_block;
    }

    int cur = node->first_block;
    for (int i = 0; i < block_offset; i++) {
        int next = fs->image.fat[cur];
        if (next == FAT_EOC) {
            if (!allocate) {
                return -1;
            }
            int new_block = allocate_block(fs);
            if (new_block < 0) {
                return -1;
            }
            fs->image.fat[cur] = new_block;
            cur = new_block;
        } else {
            cur = next;
        }
    }
    return cur;
}

void fs_init_runtime(FileSystem *fs) {
    fs->current_dir = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fs->open_files[i].used = 0;
        fs->open_files[i].inode_index = -1;
        fs->open_files[i].offset = 0;
    }
}

int fs_load(FileSystem *fs, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    size_t read_count = fread(&fs->image, sizeof(FSImage), 1, fp);
    fclose(fp);
    if (read_count != 1 || fs->image.magic != FS_MAGIC) {
        return -1;
    }
    fs_init_runtime(fs);
    return 0;
}

int fs_save(FileSystem *fs, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    size_t write_count = fwrite(&fs->image, sizeof(FSImage), 1, fp);
    fclose(fp);
    return write_count == 1 ? 0 : -1;
}

int fs_format(FileSystem *fs) {
    memset(&fs->image, 0, sizeof(FSImage));
    fs->image.magic = FS_MAGIC;
    fs->image.formatted = 1;
    fs->image.block_size = BLOCK_SIZE;
    fs->image.block_count = BLOCK_COUNT;
    for (int i = 0; i < BLOCK_COUNT; i++) {
        fs->image.fat[i] = FAT_FREE;
    }

    for (int i = 0; i < MAX_INODES; i++) {
        fs->image.inodes[i].used = 0;
    }

    int root = allocate_inode(fs, "/", 1, -1);
    fs->current_dir = root;
    return 0;
}

int fs_mkdir(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    if (find_child(fs, fs->current_dir, name) >= 0) {
        return -1;
    }
    int idx = allocate_inode(fs, name, 1, fs->current_dir);
    if (idx < 0) {
        return -1;
    }
    return add_child(fs, fs->current_dir, idx);
}

int fs_rmdir(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    int idx = find_child(fs, fs->current_dir, name);
    if (idx < 0) {
        return -1;
    }
    Inode *node = &fs->image.inodes[idx];
    if (!node->is_dir || node->child_count > 0) {
        return -1;
    }
    node->used = 0;
    return remove_child(fs, fs->current_dir, idx);
}

int fs_ls(FileSystem *fs) {
    if (!fs->image.formatted) {
        return -1;
    }
    Inode *dir = &fs->image.inodes[fs->current_dir];
    for (int i = 0; i < dir->child_count; i++) {
        int child_idx = dir->children[i];
        Inode *child = &fs->image.inodes[child_idx];
        if (!child->used) {
            continue;
        }
        printf("%s%s\t%d\n", child->name, child->is_dir ? "/" : "", child->size);
    }
    return 0;
}

int fs_cd(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    if (strcmp(name, "/") == 0) {
        fs->current_dir = 0;
        return 0;
    }
    if (strcmp(name, "..") == 0) {
        int parent = fs->image.inodes[fs->current_dir].parent;
        if (parent >= 0) {
            fs->current_dir = parent;
        }
        return 0;
    }
    int idx = find_child(fs, fs->current_dir, name);
    if (idx < 0) {
        return -1;
    }
    if (!fs->image.inodes[idx].is_dir) {
        return -1;
    }
    fs->current_dir = idx;
    return 0;
}

int fs_create(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    if (find_child(fs, fs->current_dir, name) >= 0) {
        return -1;
    }
    int idx = allocate_inode(fs, name, 0, fs->current_dir);
    if (idx < 0) {
        return -1;
    }
    return add_child(fs, fs->current_dir, idx);
}

int fs_rm(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    int idx = find_child(fs, fs->current_dir, name);
    if (idx < 0) {
        return -1;
    }
    Inode *node = &fs->image.inodes[idx];
    if (node->is_dir) {
        return -1;
    }
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fs->open_files[i].used && fs->open_files[i].inode_index == idx) {
            return -1;
        }
    }
    if (node->first_block != FAT_EOC) {
        free_chain(fs, node->first_block);
    }
    node->used = 0;
    return remove_child(fs, fs->current_dir, idx);
}

int fs_open(FileSystem *fs, const char *name) {
    if (!fs->image.formatted) {
        return -1;
    }
    int idx = find_child(fs, fs->current_dir, name);
    if (idx < 0) {
        return -1;
    }
    if (fs->image.inodes[idx].is_dir) {
        return -1;
    }
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!fs->open_files[i].used) {
            fs->open_files[i].used = 1;
            fs->open_files[i].inode_index = idx;
            fs->open_files[i].offset = 0;
            return i;
        }
    }
    return -1;
}

int fs_close(FileSystem *fs, int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;
    }
    if (!fs->open_files[fd].used) {
        return -1;
    }
    fs->open_files[fd].used = 0;
    fs->open_files[fd].inode_index = -1;
    fs->open_files[fd].offset = 0;
    return 0;
}

int fs_write(FileSystem *fs, int fd, const uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs->open_files[fd].used) {
        return -1;
    }
    int inode_idx = fs->open_files[fd].inode_index;
    Inode *node = &fs->image.inodes[inode_idx];

    size_t remaining = len;
    size_t offset = (size_t)fs->open_files[fd].offset;
    size_t written = 0;
    while (remaining > 0) {
        int block_offset = (int)(offset / BLOCK_SIZE);
        int within = (int)(offset % BLOCK_SIZE);
        int block_idx = get_block_at(fs, node, block_offset, 1);
        if (block_idx < 0) {
            return -1;
        }
        size_t copy_len = BLOCK_SIZE - (size_t)within;
        if (copy_len > remaining) {
            copy_len = remaining;
        }
        memcpy(&fs->image.data[block_idx * BLOCK_SIZE + within], buf + written, copy_len);
        offset += copy_len;
        written += copy_len;
        remaining -= copy_len;
    }

    fs->open_files[fd].offset = (int)offset;
    if ((int)offset > node->size) {
        node->size = (int)offset;
    }
    return (int)written;
}

int fs_read(FileSystem *fs, int fd, uint8_t *buf, size_t len, size_t *out_len) {
    if (out_len) {
        *out_len = 0;
    }
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs->open_files[fd].used) {
        return -1;
    }
    int inode_idx = fs->open_files[fd].inode_index;
    Inode *node = &fs->image.inodes[inode_idx];

    size_t offset = (size_t)fs->open_files[fd].offset;
    if (offset >= (size_t)node->size) {
        return 0;
    }
    size_t available = (size_t)node->size - offset;
    size_t remaining = len < available ? len : available;

    size_t read_total = 0;
    while (remaining > 0) {
        int block_offset = (int)(offset / BLOCK_SIZE);
        int within = (int)(offset % BLOCK_SIZE);
        int block_idx = get_block_at(fs, node, block_offset, 0);
        if (block_idx < 0) {
            break;
        }
        size_t copy_len = BLOCK_SIZE - (size_t)within;
        if (copy_len > remaining) {
            copy_len = remaining;
        }
        memcpy(buf + read_total, &fs->image.data[block_idx * BLOCK_SIZE + within], copy_len);
        offset += copy_len;
        read_total += copy_len;
        remaining -= copy_len;
    }

    fs->open_files[fd].offset = (int)offset;
    if (out_len) {
        *out_len = read_total;
    }
    return (int)read_total;
}
