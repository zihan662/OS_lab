#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

struct spinlock {
  int locked;        // 0: unlocked, 1: locked
  const char *name;  // Lock name for debugging
  int intr_state;    // Saved interrupt state
};

void initlock(struct spinlock *lk, const char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);

#endif