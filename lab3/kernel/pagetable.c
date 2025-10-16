#include "pagetable.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"

pagetable_t create_pagetable(void) {
  pagetable_t pt = (pagetable_t)alloc_page();
  if (pt == 0) {
    printf("create_pagetable: alloc_page failed\n",0);
    panic("create_pagetable: no memory");
  }
  memset(pt, 0, PGSIZE);
  printf("create_pagetable: allocated %p\n", pt);
  return pt;
}

pte_t* walk_create(pagetable_t pt, uint64 va) {
  if (va >= MAXVA) {
    printf("walk_create: invalid va=%p\n", va);
    panic("walk_create: va >= MAXVA");
  }

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pt[VPN_MASK(va, level)];
    if (*pte & PTE_V) {
      pt = (pagetable_t)PTE_PA(*pte);
    } else {
      pagetable_t new_pt = (pagetable_t)alloc_page();
      if (new_pt == 0) {
        printf("walk_create: alloc_page failed for level=%d\n", level);
        return 0;
      }
      memset(new_pt, 0, PGSIZE);
      *pte = PA_PTE((uint64)new_pt) | PTE_V;
      pt = new_pt;
    }
  }
  return &pt[VPN_MASK(va, 0)];
}

pte_t* walk_lookup(pagetable_t pt, uint64 va) {
  if (va >= MAXVA) {
    printf("walk_lookup: invalid va=%p\n", va);
    return 0;
  }

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pt[VPN_MASK(va, level)];
    if (!(*pte & PTE_V)) {
      return 0;
    }
    pt = (pagetable_t)PTE_PA(*pte);
  }
  return &pt[VPN_MASK(va, 0)];
}

int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm) {
  if (va % PGSIZE != 0 || pa % PGSIZE != 0) {
    printf("map_page: va=%p or pa=%p not page aligned\n", va, pa);
    panic("map_page: not page aligned");
  }

  pte_t *pte = walk_create(pt, va);
  if (pte == 0) {
    printf("map_page: walk_create failed for va=%p\n", va);
    return -1;
  }
  if (*pte & PTE_V) {
    printf("map_page: va=%p already mapped\n", va);
    panic("map_page: remap");
  }

  *pte = PA_PTE(pa) | perm | PTE_V;
  printf("map_page: mapped va=%p to pa=%p, perm=0x%x\n", va, pa, perm);
  return 0;
}
int map_region(pagetable_t pt, uint64 va, uint64 pa, uint64 size, int perm) {
  uint64 start = PGROUNDDOWN(va);
  uint64 end = PGROUNDUP(va + size);
  for (uint64 v = start, p = PGROUNDDOWN(pa); v < end; v += PGSIZE, p += PGSIZE) {
    if (map_page(pt, v, p, perm) != 0) {
      printf("map_region: map_page failed for va=%p\n", v);
      return -1;
    }
  }
  return 0;
}

static void freewalk(pagetable_t pt) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // 中间级页表
      freewalk((pagetable_t)PTE_PA(pte));
      pt[i] = 0;
    } else if (pte & PTE_V) {
      // 叶子页表项，不释放物理页（调用者负责）
      pt[i] = 0;
    }
  }
  free_page(pt);
}

void destroy_pagetable(pagetable_t pt) {
  if (pt == 0) {
    printf("destroy_pagetable: null pagetable\n",0);
    return;
  }
  freewalk(pt);
  printf("destroy_pagetable: freed pagetable %p\n", pt);
}

void dump_pagetable(pagetable_t pt, int level) {
  if (level < 0 || level > 2) {
    printf("dump_pagetable: invalid level=%d\n", level);
    panic("dump_pagetable: invalid level");
  }

  printf("Page table level %d at %p:\n", level, pt);
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (pte & PTE_V) {
      uint64 pa = PTE_PA(pte);
      printf("  Index %d: PTE=0x%x, PA=0x%x, V=%d, R=%d, W=%d, X=%d, U=%d\n",
             i, pte, pa, 
             !!(pte & PTE_V), 
             !!(pte & PTE_R),
             !!(pte & PTE_W), 
             !!(pte & PTE_X), 
             !!(pte & PTE_U));
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0 && level > 0) {
        // 中间级页表，递归打印
        dump_pagetable((pagetable_t)pa, level - 1);
      }
    }
  }
}