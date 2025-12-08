#include "types.h"
#include "string.h"

void* memset(void *dst, int c, size_t n) {
  unsigned char *d = (unsigned char*)dst;
  for (size_t i = 0; i < n; i++) {
    d[i] = (unsigned char)c;
  }
  return dst;
}

void* memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char*)dst;
  const unsigned char *s = (const unsigned char*)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char*)s1;
  const unsigned char *p2 = (const unsigned char*)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
  }
  return 0;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return (size_t)(p - s);
}

char* strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
  for (; i < n; i++) dst[i] = '\0';
  return dst;
}

int strcmp(const char *s1, const char *s2) {
  const unsigned char *p1 = (const unsigned char*)s1;
  const unsigned char *p2 = (const unsigned char*)s2;
  while (*p1 && *p2) {
    if (*p1 != *p2) return (int)*p1 - (int)*p2;
    p1++; p2++;
  }
  // 若其中一个结束，比较终止符差异
  return (int)*p1 - (int)*p2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char*)s1;
  const unsigned char *p2 = (const unsigned char*)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    if (p1[i] == '\0' || p2[i] == '\0') return (int)p1[i] - (int)p2[i];
  }
  return 0;
}