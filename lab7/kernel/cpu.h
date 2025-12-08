#ifndef CPU_H
#define CPU_H

#include "types.h"
#include "proc.h"

// 单核占位；未来可扩展到多核
#define NCPU 1

struct cpu {
  struct context context; // 调度器上下文
  struct proc *proc;      // 当前运行的进程
  int id;                 // CPU 标识（hartid）
};

extern struct cpu cpus[NCPU];
struct cpu* mycpu(void);

#endif // CPU_H
