#include "types.h"
#include "string.h"
#include "fs.h"

// 极简“文件系统”实现：仅支持一个内存文件，满足测试接口。
typedef struct {
  char name[64];
  char data[4096];
  int size;
  int exists;
} fake_file_t;

static fake_file_t g_fake = {0};
static int g_fd_in_use = -1;
static int g_fp = 0; // 当前偏移

int open(const char *path, int flags) {
  if (!path || path[0] == '\0') return -1;
  if ((flags & O_CREATE) != 0) {
    strncpy(g_fake.name, path, sizeof(g_fake.name)-1);
    g_fake.name[sizeof(g_fake.name)-1] = '\0';
    g_fake.size = 0;
    g_fake.exists = 1;
    g_fp = 0;
    g_fd_in_use = 3; // 任意非负 fd
    return g_fd_in_use;
  }
  if (g_fake.exists && strncmp(g_fake.name, path, sizeof(g_fake.name)) == 0) {
    g_fp = 0;
    g_fd_in_use = 3;
    return g_fd_in_use;
  }
  return -1;
}

int write(int fd, const void *buf, int n) {
  if (fd != g_fd_in_use || !buf || n < 0) return -1;
  if (!g_fake.exists) return -1;
  int space = (int)sizeof(g_fake.data) - g_fp;
  int w = n < space ? n : space;
  if (w > 0) {
    memcpy(g_fake.data + g_fp, buf, (size_t)w);
    g_fp += w;
    if (g_fp > g_fake.size) g_fake.size = g_fp;
  }
  return w;
}

int read(int fd, void *buf, int n) {
  if (fd != g_fd_in_use || !buf || n < 0) return -1;
  if (!g_fake.exists) return -1;
  int remain = g_fake.size - g_fp;
  if (remain <= 0) return 0;
  int r = n < remain ? n : remain;
  memcpy(buf, g_fake.data + g_fp, (size_t)r);
  g_fp += r;
  return r;
}

int close(int fd) {
  if (fd != g_fd_in_use) return -1;
  g_fd_in_use = -1;
  g_fp = 0;
  return 0;
}

int unlink(const char *path) {
  if (!path) return -1;
  if (g_fake.exists && strncmp(g_fake.name, path, sizeof(g_fake.name)) == 0) {
    g_fake.exists = 0;
    g_fake.size = 0;
    g_fake.name[0] = '\0';
    return 0;
  }
  return -1;
}