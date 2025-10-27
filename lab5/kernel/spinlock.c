#include "types.h"
#include "spinlock.h"
#include "riscv.h"
#include "printf.h"

void initlock(struct spinlock *lk, const char *name) {
  lk->locked = 0;
  lk->name = name;
  lk->intr_state = 0;
}

void acquire(struct spinlock *lk) {
  if (lk->locked) {
    panic("acquire: lock already held");
  }

  // 保存并禁用中断
  lk->intr_state = intr_get();
  intr_off();

  lk->locked = 1;
}

void release(struct spinlock *lk) {
  if (!lk->locked) {
    panic("release: lock not held");
  }

  lk->locked = 0;

  // 恢复中断状态
  intr_on(lk->intr_state);
}