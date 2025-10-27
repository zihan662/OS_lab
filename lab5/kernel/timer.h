#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void sbi_set_timer(uint64 time);
uint64 get_time(void);
void timer_interrupt(void);
void timer_init(uint64 interval_cycles);

#endif