#ifndef PMM_H
#define PMM_H

#define PGSIZE 4096
#define PGROUNDUP(addr) (((addr) + PGSIZE - 1) & ~(PGSIZE - 1))
void pmm_init(void);
void* alloc_page(void);
void free_page(void* page);
void* alloc_pages(int n);
void free_pages(void* pages, int n);

#endif