#ifndef PAGETABLE_H
#define PAGETABLE_H

#include "types.h"

typedef uint64 pte_t;
typedef pte_t* pagetable_t;

#define PGSIZE 4096
#define MAXVA (1LL << 39)
#define VPN_SHIFT(level) (12 + 9 * (level))
#define VPN_MASK(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)
#define PTE_PA(pte) (((pte) >> 10) << 12)
#define PA_PTE(pa) (((pa) >> 12) << 10)
#define PGROUNDUP(addr) (((addr) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(addr) ((addr) & ~(PGSIZE - 1))
#define PTE_V (1LL << 0)
#define PTE_R (1LL << 1)
#define PTE_W (1LL << 2)
#define PTE_X (1LL << 3)
#define PTE_U (1LL << 4)

pagetable_t create_pagetable(void);
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm);
int map_region(pagetable_t pt, uint64 va, uint64 pa, uint64 size, int perm); // 新增
void destroy_pagetable(pagetable_t pt);

// 辅助函数
pte_t* walk_create(pagetable_t pt, uint64 va);
pte_t* walk_lookup(pagetable_t pt, uint64 va);

// 调试函数
void dump_pagetable(pagetable_t pt, int level);

#endif