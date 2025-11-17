#include "types.h"
#include "riscv.h"
#include "interrupts.h"
#include "timer.h"
#include "printf.h"

static uint64 tick_interval = 1000000ULL; // default 1M cycles
static volatile uint64 g_ticks = 0;

__attribute__((weak)) void schedule_on_tick(void) {}

void sbi_set_timer(uint64 time)
{
  register uint64 a0 asm("a0") = time;
  register uint64 a7 asm("a7") = 0;
  asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

uint64 get_time(void)
{
  return r_time();
}
// Return number of timer interrupts since boot
uint64 timer_ticks(void)
{
  return g_ticks;
}

void timer_interrupt(void)
{
  g_ticks++;
  schedule_on_tick();
  uint64 next = get_time() + tick_interval;
  sbi_set_timer(next);
}

void timer_init(uint64 interval_cycles)
{
  if(interval_cycles) tick_interval = interval_cycles;
  register_interrupt(5, timer_interrupt);
  uint64 now = get_time();
  uint64 next = now + tick_interval;
  printf("timer_init: interval=%p, now=%p, next=%p\n", tick_interval, now, next);
  // set first event and enable STIE
  sbi_set_timer(next);
  enable_interrupt(5);
  printf("timer_init: STIE enabled, sie=%p, sstatus=%p\n", r_sie(), r_sstatus());
}