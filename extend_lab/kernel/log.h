#ifndef LOG_H
#define LOG_H

#include "types.h"
#include "spinlock.h"
#include "fs.h"
#include "bcache.h"

// 日志块上限（包含头部块之外的可记录数据块数量）
// 受限于固定数组，需不超过日志区大小-1
#define LOG_MAX_BLOCKS 64

// 日志头（存储在日志区起始块）
struct log_header {
  uint32 n;                      // 已记录的数据块数量
  uint32 block[LOG_MAX_BLOCKS];  // 每个数据块对应的目标块号
};

// 日志系统状态
struct log_state {
  struct spinlock lock;          // 保护日志状态
  int start;                     // 日志区起始块号
  int size;                      // 日志区大小（块数）
  int outstanding;               // 未完成的系统调用（事务）数量
  int committing;                // 是否正在提交
  int dev;                       // 设备号

  // 内部状态
  int n;                         // 当前记录的块数量
  uint32 block[LOG_MAX_BLOCKS];  // 记录的目标块号集合（去重）
};

void log_init(int dev, struct superblock *sb);
void begin_transaction(void);
void end_transaction(void);
void log_block_write(struct buffer_head *bh);
void recover_log(void);

#endif