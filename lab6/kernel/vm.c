#include "vm.h"
#include "memlayout.h"
#include "pmm.h"
#include "printf.h"
#include "riscv.h"
#include "string.h"
extern char etext[];

pagetable_t kernel_pagetable;

void kvminit(void) {
  //
  printf("kvminit: KERNBASE=%p, etext=%p, PHYSTOP=%p, UART0=%p\n", 
         KERNBASE, etext, PHYSTOP, UART0);
  //
  // 1. 创建内核页表
  kernel_pagetable = create_pagetable();
  if (kernel_pagetable == 0) {
    panic("kvminit: create_pagetable failed");
  }

  //
  uint64 text_end = PGROUNDUP((uint64)etext);
  uint64 text_size = text_end - KERNBASE;
  printf("Mapping kernel text: va=%p to %p, size=%p, perm=0x%x\n", 
         KERNBASE, text_end, text_size, PTE_R | PTE_X);
  //
  // 2. 映射内核代码段（R+X权限）
  if (map_region(kernel_pagetable, KERNBASE, KERNBASE, text_end - KERNBASE, PTE_R | PTE_X) != 0) {
    panic("kvminit: map kernel code failed");
  }

  //
  uint64 data_start = text_end;
  uint64 data_size = PHYSTOP - data_start;
  printf("Mapping kernel data: va=%p to %p, size=%p, perm=0x%x\n", 
           data_start, data_start + data_size, data_size, PTE_R | PTE_W);
  //
  // 3. 映射内核数据段（R+W权限）
  if (map_region(kernel_pagetable, data_start, data_start, data_size, PTE_R | PTE_W) != 0) {
    panic("kvminit: map kernel data failed");
  }

  //
   printf("Mapping UART0: va=%p, size=%p, perm=0x%x\n", 
         UART0, PGSIZE, PTE_R | PTE_W);
  //
  // 4. 映射设备（UART等）
  if (map_region(kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W) != 0) {
    panic("kvminit: map UART0 failed");
  }
  printf("Mapping PLIC: va=%p, size=%p, perm=0x%x\n", 
         PLIC, 0x400000, PTE_R | PTE_W);
  if (map_region(kernel_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W) != 0) {
    panic("kvminit: map PLIC failed");
  }
}

void kvminithart(void) {
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Initialize a per-process user pagetable with kernel mappings
int init_user_pagetable(pagetable_t pt) {
  if (!pt) return -1;
  uint64 text_end = PGROUNDUP((uint64)etext);
  uint64 text_sz = text_end - KERNBASE;
  uint64 data_sz = PHYSTOP - text_end;
  
  // Map kernel text (R+X, no U)
  if (map_region(pt, KERNBASE, KERNBASE, text_sz, PTE_R | PTE_X) != 0) {
    return -1;
  }
  // Map kernel data (R+W, no U)
  if (map_region(pt, text_end, text_end, data_sz, PTE_R | PTE_W) != 0) {
    return -1;
  }
  // Map trampoline (R+X)
  if (map_region(pt, TRAMPOLINE, TRAMPOLINE, PGSIZE, PTE_R | PTE_X) != 0) {
    return -1;
  }
  // Map devices
  if (map_region(pt, UART0, UART0, PGSIZE, PTE_R | PTE_W) != 0) {
    return -1;
  }
  if (map_region(pt, PLIC, PLIC, 0x400000, PTE_R | PTE_W) != 0) {
    return -1;
  }
  return 0;
}

// Copy user space (pages with PTE_U) below TRAPFRAME from src to dst
int copy_user_space(pagetable_t dst, pagetable_t src) {
  if (!dst || !src) return -1;
  for (uint64 va = 0; va < TRAPFRAME; va += PGSIZE) {
    pte_t *pte = walk_lookup(src, va);
    if (!pte) continue;
    pte_t e = *pte;
    if (!(e & PTE_V) || !(e & PTE_U)) continue;
    uint64 pa = PTE_PA(e);
    void *npa = alloc_page();
    if (!npa) return -1;
    // copy content
    memcpy(npa, (void*)pa, PGSIZE);
    int perm = e & (PTE_R | PTE_W | PTE_X | PTE_U);
    if (map_page(dst, va, (uint64)npa, perm) != 0) return -1;
  }
  return 0;
}

// Free user pages (PTE_U) and then free the page table structures
int free_user_space(pagetable_t pt) {
  if (!pt) return -1;
  for (uint64 va = 0; va < TRAPFRAME; va += PGSIZE) {
    pte_t *pte = walk_lookup(pt, va);
    if (!pte) continue;
    pte_t e = *pte;
    if ((e & PTE_V) && (e & PTE_U)) {
      void *pa = (void*)PTE_PA(e);
      free_page(pa);
      *pte = 0;
    }
  }
  destroy_pagetable(pt);
  return 0;
}
