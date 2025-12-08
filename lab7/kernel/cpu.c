#include "cpu.h"
#include "riscv.h"

struct cpu cpus[NCPU];

struct cpu* mycpu(void) {
  uint64 id = r_tp(); // entry.S 已将 hartid 写入 tp
  if (id >= NCPU) id = 0; // 单核占位
  return &cpus[id];
}
