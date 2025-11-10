#ifndef DIR_H
#define DIR_H

#include "types.h"
#include "string.h"
#include "fs.h"
#include "bcache.h"

// 文件名最大长度建议（目录项中使用变长，解析时限制）
#define DIR_MAX_NAME 255

// 目录操作接口（与任务描述一致，使用 struct inode*）
struct inode* dir_lookup(struct inode *dp, char *name, uint *poff);
int dir_link(struct inode *dp, char *name, uint inum);
int dir_unlink(struct inode *dp, char *name);

// 路径解析
struct inode* path_walk(char *path);
struct inode* path_parent(char *path, char *name);

// 调试接口
void debug_filesystem_state(void);
void debug_inode_usage(void);
void debug_disk_io(void);

#endif