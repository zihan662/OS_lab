#include "types.h"
#include "memlayout.h"
#include "spinlock.h"
#include "pmm.h"
#include "string.h"
#include "printf.h"
extern char end[];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int free_pages;
  int total_pages;
} pmm;

static void freerange(void *pa_start, void *pa_end) {
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  printf("freerange: start=%p, end=%p\n", p, pa_end);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    free_page(p);
  }
}

void pmm_init(void) {
  initlock(&pmm.lock, "pmm");
  freerange(end, (void*)PHYSTOP);
  pmm.total_pages = (PHYSTOP - (uint64)end) / PGSIZE;
  pmm.free_pages = pmm.total_pages;
}

void free_page(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP || pa == 0) {
    panic("free_page invalid pa");
  }

  memset(pa, 1, PGSIZE);  // 填充 junk

  r = (struct run*)pa;

  acquire(&pmm.lock);
  r->next = pmm.freelist;
  pmm.freelist = r;
  pmm.free_pages++;
  release(&pmm.lock);
}

void* alloc_page(void) {
  struct run *r;

  acquire(&pmm.lock);
  if (pmm.free_pages <= 0) {
    release(&pmm.lock);
    return 0;
  }
  r = pmm.freelist;
  if (r) {
    pmm.freelist = r->next;
    pmm.free_pages--;
  }
  release(&pmm.lock);

  if (r) {
    memset((char*)r, 5, PGSIZE);  // 填充 junk
  }

  return (void*)r;
}

void* alloc_pages(int n) {
  acquire(&pmm.lock);
  if (n <= 0 || n > pmm.free_pages) {
    release(&pmm.lock);
    return 0;
  }

  // 在 freelist 中查找物理地址连续的 n 个页面（链方向为高地址到低地址）
  struct run *prev = 0;
  struct run *cur = pmm.freelist;
  while (cur) {
    struct run *block_prev = prev;  // block 起点之前的节点
    struct run *block_start = cur;  // 连续块起点（高地址）
    int count = 1;

    // 向下（地址递减）累计连续页：cur->next 的地址应等于 cur 地址减去 PGSIZE
    while (cur->next && ((uint64)cur->next + PGSIZE == (uint64)cur)) {
      prev = cur;
      cur = cur->next;
      count++;
      if (count == n) break;
    }

    if (count == n) {
      // 连续段为 [block_start .. cur]，摘除该段，并断开尾
      struct run *tail_next = cur->next;
      if (block_prev == 0) {
        pmm.freelist = tail_next;
      } else {
        block_prev->next = tail_next;
      }
      cur->next = 0; // 断开与 freelist 的连接
      pmm.free_pages -= n;
      // 对连续块的每一页进行填充（从高地址向低地址），避免越界
      for (int i = 0; i < n; i++) {
        char *p = (char*)block_start - (uint64)i * PGSIZE;
        memset(p, 5, PGSIZE);
      }
      release(&pmm.lock);
      return (void*)block_start;
    }

    // 若未满足，继续从下一个节点开始尝试
    prev = cur;
    cur = cur->next;
  }

  release(&pmm.lock);
  printf("alloc_pages: failed to find %d consecutive pages\n", n);
  return 0;
}

// 成组释放 n 个连续页面。
void free_pages(void *pa, int n) {
  if (pa == 0 || n <= 0) {
    panic("free_pages invalid args");
  }

  // 基本有效性检查：页对齐与范围
  uint64 high = (uint64)pa;
  uint64 low = high - (uint64)(n - 1) * PGSIZE;
  if (((uint64)pa % PGSIZE) != 0 || low < (uint64)end || high >= PHYSTOP) {
    panic("free_pages invalid range");
  }

  // 以高到低地址顺序重建链段，保持与 freelist 的方向一致（高地址 -> 低地址）
  struct run *first = 0; // 段头为最高地址页
  struct run *last = 0;  // 迭代构建链段
  for (int i = 0; i < n; i++) {
    char *p = (char*)pa - (uint64)i * PGSIZE;
    memset(p, 1, PGSIZE);
    struct run *r = (struct run*)p;
    if (!first) {
      first = r;
      last = r;
    } else {
      last->next = r;
      last = r;
    }
  }
  // 尾节点指向原 freelist 头
  acquire(&pmm.lock);
  last->next = pmm.freelist;
  pmm.freelist = first;
  pmm.free_pages += n;
  release(&pmm.lock);
}