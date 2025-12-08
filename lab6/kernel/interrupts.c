#include "types.h"
#include "interrupts.h"
#include "memlayout.h"
#include "riscv.h"
#include "timer.h"
#include "printf.h"
#include "proc.h"
#include "syscall.h"
#include "pagetable.h"

static inline uint64 ctx_read64(void *sp, int offset){ return *(uint64*)((char*)sp + offset); }
static inline void   ctx_write64(void *sp, int offset, uint64 val){ *(uint64*)((char*)sp + offset) = val; }
// 内核侧 syscall 实现原型
extern uint64 uptime(void);
extern int sys_write(int fd, const char *buf, int n);
// 保存上下文的栈布局偏移（trap.S 中 sd 顺序）
#define CTX_OFF_A0  72
#define CTX_OFF_A1  80
#define CTX_OFF_A2  88
#define CTX_OFF_A3  96
#define CTX_OFF_A4  104
#define CTX_OFF_A5  112
#define CTX_OFF_A6  120
#define CTX_OFF_A7  128
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
  // stvec低2位为模式：0=direct。确保基地址4字节对齐。
  uint64 base = ((uint64)trap_vector) & ~0x3ULL;
  w_stvec(base);
  uint64 hart = r_tp();
  volatile uint32 *spriority = (volatile uint32*)PLIC_SPRIORITY(hart);
  *spriority = 0;
  // Debug output to verify trap init
  printf("trap_init: stvec=%p (trap_vector=%p aligned=%p)\n", r_stvec(), (void*)trap_vector, (void*)base);
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
  w_sepc(sepc + 4);
}

void handle_instruction_page_fault(uint64 sepc, uint64 stval) {
  printf("Instruction page fault: sepc=%p stval=%p\n", sepc, stval);
  uint64 va = PGROUNDDOWN(stval);
  extern pagetable_t kernel_pagetable;
  pte_t *pte = walk_lookup(kernel_pagetable, va);
  if (pte) {
    printf("fault pte: addr=%p val=0x%x pa=%p flags(VRWXUAD)=%d%d%d%d%d%d%d\n",
           pte, *pte, (void*)PTE_PA(*pte),
           !!(*pte & PTE_V), !!(*pte & PTE_R), !!(*pte & PTE_W), !!(*pte & PTE_X),
           !!(*pte & PTE_U), !!(*pte & PTE_A), !!(*pte & PTE_D));
  } else {
    printf("fault pte: not found for va=%p\n", va);
  }
  panic("Instruction page fault");
}

void handle_load_page_fault(uint64 sepc, uint64 stval) {
  printf("Load page fault: sepc=%p stval=%p satp=%p sscratch=%p\n", sepc, stval, r_satp(), r_sscratch());
  // dump instruction bytes at sepc
  unsigned char *ip = (unsigned char*)sepc;
  printf("sepc bytes: %x %x %x %x\n", ip[0], ip[1], ip[2], ip[3]);
  panic("Load page fault");
}

void handle_store_page_fault(uint64 sepc, uint64 stval) {
  printf("Store page fault: sepc=%p stval=%p\n", sepc, stval);
  uint64 s = r_sstatus();
  if ((s & SSTATUS_SPP) == 0) {
    struct proc *p = get_current_process();
    pagetable_t upt = p ? p->pagetable : 0;
    uint64 va = PGROUNDDOWN(stval);
    if (upt) {
      pte_t *upte = walk_lookup(upt, va);
      if (upte) {
        printf("user fault pte: addr=%p val=0x%x pa=%p flags(VRWXUAD)=%d%d%d%d%d%d%d\n",
               upte, *upte, (void*)PTE_PA(*upte),
               !!(*upte & PTE_V), !!(*upte & PTE_R), !!(*upte & PTE_W), !!(*upte & PTE_X),
               !!(*upte & PTE_U), !!(*upte & PTE_A), !!(*upte & PTE_D));
      } else {
        printf("user fault pte: not found for va=%p\n", va);
      }
    }
    exit_process(-1);
    return;
  }
  uint64 va = PGROUNDDOWN(stval);
  extern pagetable_t kernel_pagetable;
  pte_t *pte = walk_lookup(kernel_pagetable, va);
  if (pte) {
    printf("fault pte: addr=%p val=0x%x pa=%p flags(VRWXUAD)=%d%d%d%d%d%d%d\n",
           pte, *pte, (void*)PTE_PA(*pte),
           !!(*pte & PTE_V), !!(*pte & PTE_R), !!(*pte & PTE_W), !!(*pte & PTE_X),
           !!(*pte & PTE_U), !!(*pte & PTE_A), !!(*pte & PTE_D));
  } else {
    printf("fault pte: not found for va=%p\n", va);
  }
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
      break;
    case EXC_STORE_PAGE_FAULT:
      handle_store_page_fault(sepc, stval);
      break;
    default:
      printf("trap: unknown exception scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
      panic("Unknown exception");
  }
}

void trap_handler(uint64 scause, uint64 sepc, uint64 stval,void *ctx_sp, uint64 a7val, uint64 a0val){
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
    // Exception: syscall / faults
    uint64 code = scause & ((1ULL<<63)-1);
    if(code == EXC_LOAD_PAGE_FAULT){
      uint64 a0s = ctx_read64(ctx_sp, CTX_OFF_A0);
      uint64 a1s = ctx_read64(ctx_sp, CTX_OFF_A1);
      uint64 a2s = ctx_read64(ctx_sp, CTX_OFF_A2);
      printf("LPF ctx: a0=%p a1=%p a2=%p\n", (void*)a0s, (void*)a1s, (void*)a2s);
      handle_load_page_fault(sepc, stval);
      return;
    }
    if(code == EXC_ECALL_U || code == EXC_ECALL_S){
      // 构造统一的系统调用帧并分发
      struct syscall_frame f;
      f.a0 = ctx_read64(ctx_sp, CTX_OFF_A0);
      f.a1 = ctx_read64(ctx_sp, CTX_OFF_A1);
      f.a2 = ctx_read64(ctx_sp, CTX_OFF_A2);
      f.a3 = ctx_read64(ctx_sp, CTX_OFF_A3);
      f.a4 = ctx_read64(ctx_sp, CTX_OFF_A4);
      f.a5 = ctx_read64(ctx_sp, CTX_OFF_A5);
      f.a6 = ctx_read64(ctx_sp, CTX_OFF_A6);
      f.a7 = a7val;
      f.sepc = sepc;

      (void)a0val;

      long ret = syscall_dispatch(&f);
      ctx_write64(ctx_sp, CTX_OFF_A0, (uint64)ret);
      // 推进 sepc 跳过 ecall
      w_sepc(sepc + 4);
    } else {
      handle_exception(scause, sepc, stval);
    }
  }
}
