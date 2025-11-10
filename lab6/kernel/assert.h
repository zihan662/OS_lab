#ifndef ASSERT_H
#define ASSERT_H

#include "printf.h"

#define assert(cond) \
  do { \
    if (!(cond)) { \
      printf("Assertion failed: %s, file %s, line %d\n", #cond, __FILE__, __LINE__); \
      panic("assert failed"); \
    } \
  } while (0)

#endif