#include "types.h"
#include "interrupts.h"
#include "memlayout.h"
#include "riscv.h"
#include "timer.h"
#include "printf.h"
#include "proc.h"
#include "syscall.h"

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

void trap_handler(uint64 scause, uint64 sepc, uint64 stval,void *ctx_sp){
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
    if(code == EXC_ECALL_U || code == EXC_ECALL_S){
      // 读取 syscall 号与参数
      uint64 num = ctx_read64(ctx_sp, CTX_OFF_A7);
      uint64 a0  = ctx_read64(ctx_sp, CTX_OFF_A0);
      uint64 a1  = ctx_read64(ctx_sp, CTX_OFF_A1);
      uint64 ret = (uint64)-1;

      switch(num){
        case SYS_setpriority:
          ret = (uint64)setpriority((int)a0, (int)a1);
          break;
        case SYS_getpriority:
          ret = (uint64)getpriority((int)a0);
          break;
        case SYS_uptime:
          ret = uptime();
          break;
        case SYS_exit:
          // 退出当前进程，不再返回
          exit_process((int)a0);
          ret = 0;
          break;
        case SYS_procdump:
          // 打印进程表（详细版），返回 0
          proc_dump_detailed();
          ret = 0;
          break;
        case SYS_write: {
          uint64 a2 = ctx_read64(ctx_sp, CTX_OFF_A2);
          ret = (uint64)sys_write((int)a0, (const char*)a1, (int)a2);
          break; }
        default:
          printf("syscall: unknown num=%p sepc=%p\n", (void*)num, (void*)sepc);
          ret = (uint64)-1;
          break;
      }
      // 写回返回值并推进 sepc 跳过 ecall
      ctx_write64(ctx_sp, CTX_OFF_A0, ret);
      w_sepc(sepc + 4);
    } else {
      handle_exception(scause, sepc, stval);
    }
  }
}