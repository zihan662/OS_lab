#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

typedef void (*interrupt_handler_t)(void);

// Exception codes (scause low bits when MSB==0)
#define EXC_INST_ADDR_MISALIGNED 0
#define EXC_INST_ACCESS_FAULT    1
#define EXC_ILLEGAL_INST         2
#define EXC_BREAKPOINT           3
#define EXC_LOAD_ADDR_MISALIGNED 4
#define EXC_LOAD_ACCESS_FAULT    5
#define EXC_STORE_ADDR_MISALIGNED 6
#define EXC_STORE_ACCESS_FAULT   7
#define EXC_ECALL_U              8
#define EXC_ECALL_S              9
#define EXC_INST_PAGE_FAULT      12
#define EXC_LOAD_PAGE_FAULT      13
#define EXC_STORE_PAGE_FAULT     15

void trap_init(void);
void register_interrupt(int irq, interrupt_handler_t h);
void enable_interrupt(int irq);
void disable_interrupt(int irq);

// Exception handling APIs
void handle_exception(uint64 scause, uint64 sepc, uint64 stval);
void handle_syscall(uint64 sepc);
void handle_instruction_page_fault(uint64 sepc, uint64 stval);
void handle_load_page_fault(uint64 sepc, uint64 stval);
void handle_store_page_fault(uint64 sepc, uint64 stval);
void handle_illegal_instruction(uint64 sepc, uint64 stval);

#endif