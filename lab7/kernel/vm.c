#include "vm.h"
#include "memlayout.h"
#include "pmm.h"
#include "printf.h"
#include "riscv.h"

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