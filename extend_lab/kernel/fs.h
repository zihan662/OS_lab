#ifndef FS_H
#define FS_H

#include "types.h"

#define BLOCK_SIZE 4096
#define SUPERBLOCK_NUM 1
#define LOG_START 2
#define LOG_SIZE 30

#define MYFS_MAGIC 0x4D594653
#define MYFS_VERSION 1

#define MYFS_NDIRECT 12
#define MYFS_PTRS_PER_BLOCK (BLOCK_SIZE / 4)

enum my_filetype {
  MYFT_UNKNOWN = 0,
  MYFT_REG = 1,
  MYFT_DIR = 2,
  MYFT_SYMLINK = 3
};

struct superblock {
  uint32 magic;              // 魔数：识别文件系统
  uint32 version;            // 版本号：便于格式兼容
  uint32 block_size;         // 块大小（字节），通常为 4096
  uint32 fs_size_blocks;     // 全盘总块数
  uint32 inode_count;        // inode 总数
  uint32 free_inode_count;   // 空闲 inode 数
  uint32 free_block_count;   // 空闲数据块数
  uint32 log_start;          // 日志区起始块号
  uint32 log_size;           // 日志区大小（块数）
  uint32 inode_bitmap_start; // inode 位图起始块号
  uint32 inode_bitmap_size;  // inode 位图大小（块数）
  uint32 block_bitmap_start; // 数据块位图起始块号
  uint32 block_bitmap_size;  // 数据块位图大小（块数）
  uint32 inode_table_start;  // inode 表起始块号
  uint32 inode_table_size;   // inode 表大小（块数）
  uint32 data_start;         // 数据区起始块号
  uint32 root_inode;         // 根目录 inode 号
};

struct inode {
  uint16 mode;               // 文件模式与类型（权限位、类型位）
  uint16 uid;                // 所有者用户 ID
  uint16 gid;                // 所有者组 ID
  uint16 nlink;              // 硬链接计数
  uint32 size;               // 文件大小（字节）
  uint32 blocks;             // 已分配的数据块数量
  uint32 atime;              // 最后访问时间（秒）
  uint32 mtime;              // 最后修改时间（秒）
  uint32 ctime;              // 状态变更时间（秒）
  uint32 flags;              // 标志位（目录索引、内联数据等开关）
  uint32 xattr;              // 扩展属性块指针（未启用为 0）
  uint32 direct[MYFS_NDIRECT];// 直接块指针数组（快速定位小文件数据）
  uint32 indirect;           // 一级间接块指针（块内为数据块号数组）
  uint32 double_indirect;    // 二级间接块指针（指向若干一级间接块）
  uint8 inline_data[128];    // 内联数据（极小文件或短符号链接）
};

struct dirent {
  uint32 ino;                // 目录项对应的 inode 号
  uint8 type;                // 文件类型（见 my_filetype）
  uint16 name_len;           // 文件名长度（不含终止符）
  char name[];               // 变长文件名（柔性数组）
};

#define MYFS_MAX_DIRECT (MYFS_NDIRECT)
#define MYFS_MAX_INDIRECT (MYFS_PTRS_PER_BLOCK)
#define MYFS_MAX_DOUBLE_INDIRECT (MYFS_PTRS_PER_BLOCK * MYFS_PTRS_PER_BLOCK)
#define MYFS_MAX_FILE_BLOCKS (MYFS_MAX_DIRECT + MYFS_MAX_INDIRECT + MYFS_MAX_DOUBLE_INDIRECT)
#define MYFS_MAX_FILE_SIZE ((uint64)MYFS_MAX_FILE_BLOCKS * (uint64)BLOCK_SIZE)

// ===== 用户态风格文件接口（内核测试最小包装） =====
// 打开标志
#define O_CREATE 0x1
#define O_RDWR   0x2
#define O_RDONLY 0x4

// 接口原型（最小实现由 fs.c 提供）
int open(const char *path, int flags);
int write(int fd, const void *buf, int n);
int read(int fd, void *buf, int n);
int close(int fd);
int unlink(const char *path);
#endif