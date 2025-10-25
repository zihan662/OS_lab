#include "types.h"
#include "interrupts.h"
#include "memlayout.h"
#include "riscv.h"
#include "timer.h"
#include "printf.h"

#define MAX_IRQS 128

struct irq_node { interrupt_handler_t fn; struct irq_node *next; };
static struct irq_node *irq_table[MAX_IRQS];
static struct irq_node irq_pool[256];
static int irq_pool_used = 0;

extern void trap_vector(void);

static void add_handler(int irq, interrupt_handler_t h){
  if(irq < 0 || irq >= MAX_IRQS || h == 0) return;
  struct irq_node *n = &irq_pool[irq_pool_used++ % 256];
  n->fn = h;
  n->next = irq_table[irq];
  irq_table[irq] = n;
}

void register_interrupt(int irq, interrupt_handler_t h){
  if(irq < 0 || irq >= MAX_IRQS){
    return;
  }
  if(h == 0){
    irq_table[irq] = 0;
    return;
  }
  add_handler(irq, h);
}

static void plic_enable(int irq){
  uint64 hart = r_tp();
  volatile uint32 *senable = (volatile uint32*)PLIC_SENABLE(hart);
  volatile uint32 *spriority = (volatile uint32*)PLIC_SPRIORITY(hart);
  volatile uint32 *priority = (volatile uint32*)(PLIC_PRIORITY + 4*irq);
  *priority = 1;
  *spriority = 0;
  senable[irq/32] |= (1u << (irq % 32));
}

static void plic_disable(int irq){
  uint64 hart = r_tp();
  volatile uint32 *senable = (volatile uint32*)PLIC_SENABLE(hart);
  senable[irq/32] &= ~(1u << (irq % 32));
}

void enable_interrupt(int irq){
  if(irq == 5){
    w_sie(r_sie() | SIE_STIE);
  } else {
    plic_enable(irq);
    w_sie(r_sie() | SIE_SEIE);
  }
  // enable global supervisor interrupts
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

void disable_interrupt(int irq){
  if(irq == 5){
    w_sie(r_sie() & ~SIE_STIE);
  } else {
    plic_disable(irq);
  }
}

void trap_init(void){
  w_stvec((uint64)trap_vector);
  uint64 hart = r_tp();
  volatile uint32 *spriority = (volatile uint32*)PLIC_SPRIORITY(hart);
  *spriority = 0;
  // Debug output to verify trap init
  printf("trap_init: stvec=%p (trap_vector=%p)\n", r_stvec(), (void*)trap_vector);
  printf("trap_init: hart=%d, PLIC SPRIORITY addr=%p, value=%d\n", (int)hart, spriority, *spriority);
  printf("trap_init: sie=%p, sstatus=%p\n", r_sie(), r_sstatus());
}

static void dispatch_irq(int irq){
  struct irq_node *n = irq_table[irq];
  while(n){
    if(n->fn) n->fn();
    n = n->next;
  }
}

// ---- Exception handling implementations ----
void handle_syscall(uint64 sepc) {
  // Advance sepc to skip the ecall instruction
  printf("handle_syscall: sepc=%p\n", sepc);
  w_sepc(sepc + 4);
}

void handle_instruction_page_fault(uint64 sepc, uint64 stval) {
  printf("Instruction page fault: sepc=%p stval=%p\n", sepc, stval);
  panic("Instruction page fault");
}

void handle_load_page_fault(uint64 sepc, uint64 stval) {
  printf("Load page fault: sepc=%p stval=%p\n", sepc, stval);
  panic("Load page fault");
}

void handle_store_page_fault(uint64 sepc, uint64 stval) {
  printf("Store page fault: sepc=%p stval=%p\n", sepc, stval);
  panic("Store page fault");
}

void handle_illegal_instruction(uint64 sepc, uint64 stval) {
  printf("Illegal instruction: sepc=%p stval=%p\n", sepc, stval);
  // Skip offending instruction to allow testing to proceed
  w_sepc(sepc + 4);
}

void handle_exception(uint64 scause, uint64 sepc, uint64 stval) {
  uint64 code = scause & ((1ULL<<63)-1);
  switch (code) {
    case EXC_ECALL_S:
      handle_syscall(sepc);
      break;
    case EXC_ILLEGAL_INST:
      handle_illegal_instruction(sepc, stval);
      break;
    case EXC_INST_PAGE_FAULT:
      handle_instruction_page_fault(sepc, stval);
      break;
    case EXC_LOAD_PAGE_FAULT:
      handle_load_page_fault(sepc, stval);
      break;
    case EXC_STORE_PAGE_FAULT:
      handle_store_page_fault(sepc, stval);
      break;
    default:
      printf("trap: unknown exception scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
      panic("Unknown exception");
  }
}

void trap_handler(uint64 scause, uint64 sepc, uint64 stval){
  if(scause >> 63){
    uint64 code = scause & ((1ULL<<63)-1);
    if(code == 5){
      dispatch_irq(5);
    } else if(code == 9){
      uint64 hart = r_tp();
      volatile uint32 *claim = (volatile uint32*)PLIC_SCLAIM(hart);
      uint32 irq = *claim;
      if(irq){
        dispatch_irq(irq);
        *claim = irq;
      }
    }
  } else {
    // Exception: handle using exception handlers
    handle_exception(scause, sepc, stval);
  }
}