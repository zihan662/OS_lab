#include "types.h"
#include "string.h"
#include "printf.h"
#include "fs.h"
#include "bcache.h"
#include "dir.h"

// 读取超级块到 sb_out
static int read_superblock(int dev, struct superblock *sb_out) {
  if (!sb_out) return -1;
  struct buffer_head *bh = get_block(dev, SUPERBLOCK_NUM);
  if (!bh || !bh->valid) { if (bh) put_block(bh); return -1; }
  memcpy(sb_out, bh->data, sizeof(struct superblock));
  put_block(bh);
  if (sb_out->magic != MYFS_MAGIC) return -1;
  return 0;
}

// 从 inode 表加载指定 inode 号到 out
static int load_inode_by_inum(int dev, const struct superblock *sb, uint32 inum, struct inode *out) {
  if (!sb || !out) return -1;
  if (sb->inode_table_start == 0 || sb->inode_table_size == 0) return -1;
  uint32 ipb = (uint32)(BLOCK_SIZE / sizeof(struct inode));
  if (ipb == 0) return -1;
  if (inum >= sb->inode_count) return -1;
  uint32 blk_index = inum / ipb;
  uint32 off_index = inum % ipb;
  if (blk_index >= sb->inode_table_size) return -1;
  uint32 bno = sb->inode_table_start + blk_index;
  struct buffer_head *bh = get_block(dev, bno);
  if (!bh || !bh->valid) { if (bh) put_block(bh); return -1; }
  char *base = bh->data + (off_index * sizeof(struct inode));
  memcpy(out, base, sizeof(struct inode));
  put_block(bh);
  return 0;
}

// 简易根目录获取：读取超级块，并从 inode 表加载根 inode
static struct inode *get_root_inode(void) {
  static struct inode g_root_inode;
  static int g_loaded = 0;
  if (g_loaded) return &g_root_inode;

  struct superblock sb;
  int dev = 0; // 默认设备号 0；可根据挂载点扩展
  if (read_superblock(dev, &sb) < 0) {
    printf("dir: read superblock failed\n");
    return 0;
  }
  if (load_inode_by_inum(dev, &sb, sb.root_inode, &g_root_inode) < 0) {
    printf("dir: load root inode failed (inum=%u)\n", sb.root_inode);
    return 0;
  }
  g_loaded = 1;
  return &g_root_inode;
}

// 解析目录块中的下一个目录项，基于变长 my_dirent 格式
// 输入：块数据指针 data，偏移 *off（块内偏移）
// 输出：填充 ino/type/name，返回 1 表示成功读取一项，0 表示到达末尾或数据无效
static int dirent_next(char *data, uint *off, uint32 *ino_out, uint8 *type_out, char *name_out, uint *name_len_out) {
  if (*off >= BLOCK_SIZE) return 0;
  char *p = data + *off;
  // 读取固定头：ino(4) + type(1) + name_len(2)
  uint32 ino = *(uint32*)(p);
  uint8 type = *(uint8*)(p + 4);
  uint16 name_len = *(uint16*)(p + 5);
  if (name_len == 0 || name_len > DIR_MAX_NAME) {
    // 非法，终止扫描
    return 0;
  }
  // 读取名称
  const char *namep = p + 7;
  if (name_out) {
    memcpy(name_out, namep, name_len);
    name_out[name_len] = '\0';
  }
  if (ino_out) *ino_out = ino;
  if (type_out) *type_out = type;
  if (name_len_out) *name_len_out = name_len;

  // 前进到下一个目录项
  *off += (uint)(7 + name_len);
  return 1;
}

// 查找目录项：在线性扫描 dp 的直接块中查找名称匹配的项
struct inode* dir_lookup(struct inode *dp, char *name, uint *poff) {
  if (!dp || !name) return 0;
  size_t namelen = strlen(name);
  if (namelen == 0 || namelen > DIR_MAX_NAME) return 0;

  for (uint i = 0; i < MYFS_NDIRECT; i++) {
    uint32 bno = dp->direct[i];
    if (bno == 0) continue;
    struct buffer_head *bh = get_block(0, bno);
    if (!bh || !bh->valid) { if (bh) put_block(bh); continue; }
    uint off = 0;
    while (off < BLOCK_SIZE) {
      uint32 ino; uint8 type; char nm[DIR_MAX_NAME+1]; uint nlen;
      uint prev_off = off;
      if (!dirent_next(bh->data, &off, &ino, &type, nm, &nlen)) break;
      if (nlen == namelen && memcmp(nm, name, namelen) == 0) {
        if (poff) *poff = (i * BLOCK_SIZE) + prev_off;
        // TODO: 通过 inode 号加载实际 inode 结构（需要 inode 表接口）
        put_block(bh);
        return 0; // 占位：暂不返回实际 inode
      }
    }
    put_block(bh);
  }
  return 0;
}

// 在目录末尾追加一个目录项（简化：仅使用直接块）
int dir_link(struct inode *dp, char *name, uint inum) {
  if (!dp || !name) return -1;
  size_t namelen = strlen(name);
  if (namelen == 0 || namelen > DIR_MAX_NAME) return -1;

  // 简化策略：找到第一个可容纳该项的直接块，或使用空块
  for (uint i = 0; i < MYFS_NDIRECT; i++) {
    uint32 bno = dp->direct[i];
    if (bno == 0) {
      // 需要分配块：此处留空，依赖 myfs_alloc_block
      return -1;
    }
    struct buffer_head *bh = get_block(0, bno);
    if (!bh || !bh->valid) { if (bh) put_block(bh); continue; }
    uint off = 0;
    while (off < BLOCK_SIZE) {
      // 检查是否越界或可容纳
      uint need = (uint)(7 + namelen);
      if (off + need > BLOCK_SIZE) break;
      // 写入目录项
      char *p = bh->data + off;
      *(uint32*)p = (uint32)inum;
      *(uint8*)(p + 4) = (uint8)MYFT_REG; // 默认为普通文件；调用者应按需传递或修改
      *(uint16*)(p + 5) = (uint16)namelen;
      memcpy(p + 7, name, namelen);
      bh->dirty = 1;
      sync_block(bh);
      put_block(bh);
      return 0;
    }
    put_block(bh);
  }
  return -1;
}

// 线性扫描，找到名称并通过零化 ino 标记删除（简化删除）
int dir_unlink(struct inode *dp, char *name) {
  if (!dp || !name) return -1;
  size_t namelen = strlen(name);
  if (namelen == 0 || namelen > DIR_MAX_NAME) return -1;

  for (uint i = 0; i < MYFS_NDIRECT; i++) {
    uint32 bno = dp->direct[i];
    if (bno == 0) continue;
    struct buffer_head *bh = get_block(0, bno);
    if (!bh || !bh->valid) { if (bh) put_block(bh); continue; }
    uint off = 0;
    while (off < BLOCK_SIZE) {
      uint32 ino; uint8 type; char nm[DIR_MAX_NAME+1]; uint nlen; uint prev_off = off;
      if (!dirent_next(bh->data, &off, &ino, &type, nm, &nlen)) break;
      if (nlen == namelen && memcmp(nm, name, namelen) == 0) {
        char *p = bh->data + prev_off;
        *(uint32*)p = 0; // 标记为空
        bh->dirty = 1;
        sync_block(bh);
        put_block(bh);
        return 0;
      }
    }
    put_block(bh);
  }
  return -1;
}

// 路径解析：从根目录开始，逐段查找
struct inode* path_walk(char *path) {
  if (!path || path[0] == '\0') return 0;
  struct inode *ip = get_root_inode();
  if (!ip) return 0;

  char seg[DIR_MAX_NAME+1];
  uint idx = 0;
  while (path[idx] != '\0') {
    // 跳过多个 '/'
    while (path[idx] == '/') idx++;
    if (path[idx] == '\0') break;
    // 抽取一段
    uint s = 0;
    while (path[idx] != '\0' && path[idx] != '/' && s < DIR_MAX_NAME) {
      seg[s++] = path[idx++];
    }
    seg[s] = '\0';
    // 处理 '.' 与 '..'（简化：忽略，留作扩展）
    // 正常查找
    ip = dir_lookup(ip, seg, 0);
    if (!ip) return 0;
  }
  return ip;
}

// 返回父目录 inode，并输出最后一段名字到 name
struct inode* path_parent(char *path, char *name) {
  if (!path || !name) return 0;
  struct inode *ip = get_root_inode();
  if (!ip) return 0;

  char seg[DIR_MAX_NAME+1];
  uint idx = 0; struct inode *parent = ip;
  while (path[idx] != '\0') {
    while (path[idx] == '/') idx++;
    if (path[idx] == '\0') break;
    uint s = 0;
    while (path[idx] != '\0' && path[idx] != '/' && s < DIR_MAX_NAME) {
      seg[s++] = path[idx++];
    }
    seg[s] = '\0';
    // 预读下一个是否结束
    uint tmp = idx; while (path[tmp] == '/') tmp++;
    if (path[tmp] == '\0') {
      // 最后一段是子名
      strncpy(name, seg, DIR_MAX_NAME);
      name[DIR_MAX_NAME] = '\0';
      return parent;
    }
    // 继续下降
    parent = dir_lookup(parent, seg, 0);
    if (!parent) return 0;
  }
  return 0;
}

// ===== 调试函数实现（放在现有 dir.c 内） =====
void debug_filesystem_state(void) {
  printf("=== Filesystem Debug Info ===\n");
  struct superblock sb;
  if (read_superblock(0, &sb) < 0) {
    printf("Superblock read failed or magic mismatch\n");
    return;
  }
  printf("Total blocks: %u\n", sb.fs_size_blocks);
  printf("Free blocks: %u\n", sb.free_block_count);
  printf("Free inodes: %u\n", sb.free_inode_count);
  printf("Buffer cache hits: %llu\n", (unsigned long long)buffer_cache_hits);
  printf("Buffer cache misses: %llu\n", (unsigned long long)buffer_cache_misses);
}

void debug_inode_usage(void) {
  printf("=== Inode Usage ===\n");
  // 说明：当前项目尚未实现 inode 缓存(icache)与 NINODE，无法逐项打印。
  // 一旦引入 inode 缓存结构，可在此遍历并输出引用计数等信息。
  printf("inode 缓存未实现，暂无法追踪。\n");
}

void debug_disk_io(void) {
  printf("=== Disk I/O Statistics ===\n");
  printf("Disk reads: %p\n", (void*)disk_read_count);
  printf("Disk writes: %p\n", (void*)disk_write_count);
}