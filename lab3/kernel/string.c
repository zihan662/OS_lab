#include "types.h"
#include "string.h"

void* memset(void *dst, int c, uint64 n) {
  char *d = (char*)dst;
  for (uint64 i = 0; i < n; i++) {
    d[i] = (char)c;
  }
  return dst;
}