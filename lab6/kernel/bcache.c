#include "types.h"
#include "printf.h"
#include "spinlock.h"
#include "bcache.h"
#include "string.h"

// 静态缓冲池与哈希桶
static struct buffer_head bh_pool[BCACHE_NBUFS];
static char bh_data[BCACHE_NBUFS][BLOCK_SIZE];
static struct buffer_head *buckets[BCACHE_NBUCKETS];

// LRU 双向链表的哑元头结点
static struct buffer_head lru_head;
static struct spinlock bcache_lock;

// 统计计数器
uint64 buffer_cache_hits = 0;
uint64 buffer_cache_misses = 0;
uint64 disk_read_count = 0;
uint64 disk_write_count = 0;

// 简单设备 I/O 桩函数（后续应由块设备驱动实现）
static int block_read(uint dev, uint32 block, void *dst) {
  // TODO: 接入真实设备读；当前用零填充模拟成功
  (void)dev; (void)block;
  memset(dst, 0, BLOCK_SIZE);
  return 0; // 成功
}

static int block_write(uint dev, uint32 block, const void *src) {
  // TODO: 接入真实设备写；当前直接返回成功
  (void)dev; (void)block; (void)src;
  return 0; // 成功
}

static inline uint hash_index(uint dev, uint32 block) {
  return (dev ^ block) & (BCACHE_NBUCKETS - 1);
}

static void lru_init(void) {
  lru_head.lru_next = &lru_head;
  lru_head.lru_prev = &lru_head;
}

static void lru_remove(struct buffer_head *bh) {
  bh->lru_prev->lru_next = bh->lru_next;
  bh->lru_next->lru_prev = bh->lru_prev;
  bh->lru_next = bh->lru_prev = 0;
}

static void lru_insert_mru(struct buffer_head *bh) {
  // 插入到头部（最新使用）
  bh->lru_next = lru_head.lru_next;
  bh->lru_prev = &lru_head;
  lru_head.lru_next->lru_prev = bh;
  lru_head.lru_next = bh;
}

static void lru_insert_lru(struct buffer_head *bh) {
  // 插入到尾部（最旧）
  bh->lru_next = &lru_head;
  bh->lru_prev = lru_head.lru_prev;
  lru_head.lru_prev->lru_next = bh;
  lru_head.lru_prev = bh;
}

static struct buffer_head *hash_lookup(uint dev, uint32 block) {
  struct buffer_head *cur = buckets[hash_index(dev, block)];
  while (cur) {
    if (cur->dev == dev && cur->block_num == block)
      return cur;
    cur = cur->next;
  }
  return 0;
}

static void hash_remove(struct buffer_head *bh) {
  uint idx = hash_index(bh->dev, bh->block_num);
  struct buffer_head **pp = &buckets[idx];
  while (*pp) {
    if (*pp == bh) {
      *pp = bh->next;
      bh->next = 0;
      return;
    }
    pp = &(*pp)->next;
  }
}

static void hash_insert(struct buffer_head *bh) {
  uint idx = hash_index(bh->dev, bh->block_num);
  bh->next = buckets[idx];
  buckets[idx] = bh;
}

void bcache_init(void) {
  initlock(&bcache_lock, "bcache");
  lru_init();
  buffer_cache_hits = 0;
  buffer_cache_misses = 0;
  disk_read_count = 0;
  disk_write_count = 0;
  for (uint i = 0; i < BCACHE_NBUCKETS; i++) buckets[i] = 0;
  for (uint i = 0; i < BCACHE_NBUFS; i++) {
    struct buffer_head *bh = &bh_pool[i];
    bh->dev = 0;
    bh->block_num = 0;
    bh->data = bh_data[i];
    bh->dirty = 0;
    bh->ref_count = 0;
    bh->valid = 0;
    bh->error = 0;
    bh->next = 0;
    bh->lru_next = bh->lru_prev = 0;
    lru_insert_lru(bh);
  }
  printf("bcache: %d bufs, %d buckets, block=%d\n", BCACHE_NBUFS, BCACHE_NBUCKETS, BLOCK_SIZE);
}

// 选择可替换的缓存块：从 LRU 尾部向前扫描，找 ref_count==0 的块
static struct buffer_head *select_victim(void) {
  struct buffer_head *cur = lru_head.lru_prev;
  while (cur != &lru_head) {
    if (cur->ref_count == 0)
      return cur;
    cur = cur->lru_prev;
  }
  return 0;
}

struct buffer_head* get_block(uint dev, uint block) {
  acquire(&bcache_lock);

  // 快速命中
  struct buffer_head *bh = hash_lookup(dev, block);
  if (bh) {
    buffer_cache_hits++;
    bh->ref_count++;
    // 触发 MRU 调整
    lru_remove(bh);
    lru_insert_mru(bh);
    release(&bcache_lock);
    return bh;
  }

  // 需要替换
  buffer_cache_misses++;
  bh = select_victim();
  if (!bh) {
    // 没有可用缓存，返回空
    release(&bcache_lock);
    printf("bcache: no victim available\n");
    return 0;
  }

  // 如需写回则同步
  if (bh->dirty && bh->valid) {
    int rc = block_write(bh->dev, bh->block_num, bh->data);
    disk_write_count++;
    if (rc < 0) {
      bh->error = 1;
      // 写回失败，不安全替换，直接返回失败
      release(&bcache_lock);
      printf("bcache: writeback failed dev=%u blk=%u\n", bh->dev, bh->block_num);
      return 0;
    }
    bh->dirty = 0;
  }

  // 从旧哈希桶移除与 LRU 中移除
  hash_remove(bh);
  lru_remove(bh);

  // 重新设定元信息并加载新块
  bh->dev = dev;
  bh->block_num = block;
  bh->ref_count = 1;
  bh->valid = 0;
  bh->error = 0;
  // 插入到新哈希桶与 MRU
  hash_insert(bh);
  lru_insert_mru(bh);

  // 解锁期间进行 I/O？简单实现：保持锁，避免并发破坏状态
  int rc = block_read(dev, block, bh->data);
  disk_read_count++;
  if (rc < 0) {
    bh->valid = 0;
    bh->error = 1;
  } else {
    bh->valid = 1;
  }

  release(&bcache_lock);
  return bh;
}

void put_block(struct buffer_head *bh) {
  if (!bh) return;
  acquire(&bcache_lock);
  if (bh->ref_count <= 0) {
    printf("bcache: put_block on unreferenced buffer dev=%u blk=%u\n", bh->dev, bh->block_num);
  } else {
    bh->ref_count--;
  }
  // 放回 LRU 尾部（老化）
  lru_remove(bh);
  lru_insert_lru(bh);
  release(&bcache_lock);
}

void sync_block(struct buffer_head *bh) {
  if (!bh) return;
  acquire(&bcache_lock);
  if (bh->dirty && bh->valid) {
    int rc = block_write(bh->dev, bh->block_num, bh->data);
    disk_write_count++;
    if (rc < 0) {
      bh->error = 1;
      printf("bcache: sync write failed dev=%u blk=%u\n", bh->dev, bh->block_num);
    } else {
      bh->dirty = 0;
    }
  }
  release(&bcache_lock);
}

void flush_all_blocks(uint dev) {
  acquire(&bcache_lock);
  for (uint i = 0; i < BCACHE_NBUFS; i++) {
    struct buffer_head *bh = &bh_pool[i];
    if (bh->dev == dev && bh->dirty && bh->valid) {
      int rc = block_write(bh->dev, bh->block_num, bh->data);
      disk_write_count++;
      if (rc < 0) {
        bh->error = 1;
        printf("bcache: flush failed dev=%u blk=%u\n", bh->dev, bh->block_num);
      } else {
        bh->dirty = 0;
      }
    }
  }
  release(&bcache_lock);
}