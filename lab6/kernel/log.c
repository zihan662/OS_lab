#include "types.h"
#include "string.h"
#include "printf.h"
#include "spinlock.h"
#include "bcache.h"
#include "fs.h"
#include "log.h"

static struct log_state g_log;

// 将内存中的日志头写入日志起始块
static void write_log_header(void) {
  struct buffer_head *hdr = get_block(g_log.dev, (uint)g_log.start);
  if (!hdr) { printf("log: cannot get header block\n"); return; }
  struct log_header lh;
  memset(&lh, 0, sizeof(lh));
  lh.n = g_log.n;
  for (int i = 0; i < g_log.n && i < LOG_MAX_BLOCKS; i++) {
    lh.block[i] = g_log.block[i];
  }
  memcpy(hdr->data, &lh, sizeof(lh));
  hdr->dirty = 1;
  sync_block(hdr);
  put_block(hdr);
}

// 清空日志头（n=0），表示日志区为空
static void clear_log_header(void) {
  g_log.n = 0;
  write_log_header();
}

// 将要提交的数据块复制到日志区的数据块中（start+1..start+n）
static int write_log_data(void) {
  for (int i = 0; i < g_log.n; i++) {
    uint32 target_bno = g_log.block[i];
    // 获取源数据（目标块的最新内容）
    struct buffer_head *src = get_block(g_log.dev, (uint)target_bno);
    if (!src || !src->valid) {
      if (src) put_block(src);
      printf("log: read target block failed dev=%d blk=%u\n", g_log.dev, target_bno);
      return -1;
    }

    // 获取日志区对应数据块
    uint32 log_bno = (uint32)(g_log.start + 1 + i);
    struct buffer_head *dst = get_block(g_log.dev, (uint)log_bno);
    if (!dst) {
      put_block(src);
      printf("log: get log block failed dev=%d blk=%u\n", g_log.dev, log_bno);
      return -1;
    }

    memcpy(dst->data, src->data, BLOCK_SIZE);
    dst->dirty = 1;
    sync_block(dst);
    put_block(dst);
    put_block(src);
  }
  return 0;
}

// 将日志数据应用到目标块（home blocks），确保持久化
static int install_trans(void) {
  for (int i = 0; i < g_log.n; i++) {
    uint32 target_bno = g_log.block[i];
    uint32 log_bno = (uint32)(g_log.start + 1 + i);

    // 读出日志数据块
    struct buffer_head *logbh = get_block(g_log.dev, (uint)log_bno);
    if (!logbh || !logbh->valid) {
      if (logbh) put_block(logbh);
      printf("log: install read log block failed dev=%d blk=%u\n", g_log.dev, log_bno);
      return -1;
    }

    // 写入目标块
    struct buffer_head *dst = get_block(g_log.dev, (uint)target_bno);
    if (!dst) {
      put_block(logbh);
      printf("log: install get target block failed dev=%d blk=%u\n", g_log.dev, target_bno);
      return -1;
    }

    memcpy(dst->data, logbh->data, BLOCK_SIZE);
    dst->dirty = 1;
    sync_block(dst);

    put_block(dst);
    put_block(logbh);
  }
  return 0;
}

void log_init(int dev, struct superblock *sb) {
  initlock(&g_log.lock, "log");
  g_log.start = (int)sb->log_start;
  g_log.size = (int)sb->log_size;
  g_log.outstanding = 0;
  g_log.committing = 0;
  g_log.dev = dev;
  g_log.n = 0;

  // 恢复可能遗留的事务
  recover_log();
  printf("log: init start=%d size=%d dev=%d\n", g_log.start, g_log.size, g_log.dev);
}

void begin_transaction(void) {
  acquire(&g_log.lock);
  while (g_log.committing) {
    // 简化：忙等直到提交完成
    release(&g_log.lock);
    // 主动让出（若有调度器）；此处简单重新获取
    acquire(&g_log.lock);
  }
  g_log.outstanding++;
  release(&g_log.lock);
}

static void commit_transaction(void) {
  // 写日志数据
  if (write_log_data() < 0) {
    printf("log: write_log_data failed\n");
    return;
  }
  // 写提交头（幂等性保障）
  write_log_header();
  // 安装事务到 home blocks
  if (install_trans() < 0) {
    printf("log: install_trans failed\n");
  }
  // 清空日志头
  clear_log_header();
  // 重置缓冲的块集合
  g_log.n = 0;
}

void end_transaction(void) {
  acquire(&g_log.lock);
  g_log.outstanding--;
  if (g_log.outstanding < 0) {
    printf("log: negative outstanding\n");
    g_log.outstanding = 0;
  }

  if (g_log.committing) {
    // 已在提交，直接返回
    release(&g_log.lock);
    return;
  }

  // 若没有正在进行的事务且存在待提交的块，则提交
  if (g_log.outstanding == 0 && g_log.n > 0) {
    g_log.committing = 1;
    release(&g_log.lock);

    commit_transaction();

    acquire(&g_log.lock);
    g_log.committing = 0;
  }
  release(&g_log.lock);
}

// 记录某个块的写操作（加入待提交集合，去重）
void log_block_write(struct buffer_head *bh) {
  if (!bh) return;
  acquire(&g_log.lock);
  // 简易处理：防止日志超额
  if (g_log.n >= (g_log.size - 1) || g_log.n >= LOG_MAX_BLOCKS) {
    // 日志满：此事务将持续积压，等待 end_transaction 提交
    // 也可扩展为：主动提交当前日志（需要约束 begin/end 的语义）
    printf("log: full, block ignored dev=%d blk=%u\n", g_log.dev, bh->block_num);
    release(&g_log.lock);
    return;
  }
  // 去重
  for (int i = 0; i < g_log.n; i++) {
    if (g_log.block[i] == bh->block_num) {
      release(&g_log.lock);
      return;
    }
  }
  g_log.block[g_log.n++] = bh->block_num;
  release(&g_log.lock);
}

// 读取日志头并安装事务，确保幂等恢复
void recover_log(void) {
  struct buffer_head *hdr = get_block(g_log.dev, (uint)g_log.start);
  if (!hdr) {
    printf("log: recover cannot read header\n");
    return;
  }
  struct log_header lh;
  memset(&lh, 0, sizeof(lh));
  memcpy(&lh, hdr->data, sizeof(lh));
  put_block(hdr);

  if (lh.n > 0 && lh.n <= LOG_MAX_BLOCKS && lh.n <= (uint32)(g_log.size - 1)) {
    // 应用事务
    for (uint32 i = 0; i < lh.n; i++) {
      uint32 target_bno = lh.block[i];
      uint32 log_bno = (uint32)(g_log.start + 1 + i);

      struct buffer_head *logbh = get_block(g_log.dev, (uint)log_bno);
      if (!logbh || !logbh->valid) { if (logbh) put_block(logbh); continue; }
      struct buffer_head *dst = get_block(g_log.dev, (uint)target_bno);
      if (!dst) { put_block(logbh); continue; }

      memcpy(dst->data, logbh->data, BLOCK_SIZE);
      dst->dirty = 1;
      sync_block(dst);

      put_block(dst);
      put_block(logbh);
    }
    // 清空日志头
    clear_log_header();
  }
}

