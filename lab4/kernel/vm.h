#ifndef VM_H
#define VM_H

#include "types.h"
#include "pagetable.h"

extern pagetable_t kernel_pagetable;

void kvminit(void);
void kvminithart(void);

#endif