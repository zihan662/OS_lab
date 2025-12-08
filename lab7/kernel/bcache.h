#ifndef BCACHE_H
#define BCACHE_H
#include "types.h"
#include "fs.h"

// 缓存桶数量（哈希表大小，需为 2 的幂以便按位与散列）
#define BCACHE_NBUCKETS 64
// 缓存的 buffer 数量（固定大小池）
#define BCACHE_NBUFS    64

// 块缓冲头：描述缓存的一个块
struct buffer_head {
  uint   dev;              // 设备号（简单场景一个设备即可）
  uint32 block_num;        // 块号
  char  *data;             // 数据指针（指向内部静态缓冲区）
  int    dirty;            // 脏位：1 表示需要写回
  int    ref_count;        // 引用计数：>0 表示正在被使用
  int    valid;            // 数据是否有效（读入成功）
  int    error;            // 最近一次 I/O 是否出错
  struct buffer_head *next;     // 哈希桶链表
  struct buffer_head *lru_next; // LRU 双向链
  struct buffer_head *lru_prev; // LRU 双向链
};

// 关键接口
struct buffer_head* get_block(uint dev, uint block); // 获取（或加载）指定块
void put_block(struct buffer_head *bh);              // 释放引用（放回 LRU）
void sync_block(struct buffer_head *bh);             // 同步单块写回
void flush_all_blocks(uint dev);                     // 写回设备上所有脏块

// 初始化缓存（在系统启动时调用）
void bcache_init(void);

// 调试计数器（由 bcache.c 维护）
extern uint64 buffer_cache_hits;
extern uint64 buffer_cache_misses;
extern uint64 disk_read_count;
extern uint64 disk_write_count;
#endif
