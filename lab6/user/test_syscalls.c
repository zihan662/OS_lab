#include "user.h"
#include "../kernel/types.h"
#include "../kernel/pagetable.h"

static void test_basic_syscalls(void) {
  usys_printf(1, "Testing basic syscalls (user mode)\n");
  int pid = usys_getpid();
  usys_printf(1, "getpid -> %d\n", pid);

  int child = usys_fork();
  if (child == 0) {
    // child
    usys_printf(1, "child: pid=%d, exiting...\n", usys_getpid());
    usys_exit();
  } else if (child > 0) {
    // parent waits (current wait returns without status)
    (void)usys_wait();
    usys_printf(1, "parent: child %d exited\n", child);
  } else {
    usys_printf(1, "fork not supported (expected in kernel-thread mode)\n");
  }
}

static void test_parameter_passing(void) {
  usys_printf(1, "Testing parameter passing (user mode)\n");
  const char *msg = "Hello, World!";
  int r = usys_write(1, msg, 13);
  usys_printf(1, "write(1, msg, 13) -> %d\n", r);

  r = usys_write(-1, msg, 5);
  usys_printf(1, "write(-1, msg, 5) -> %d (expect -1)\n", r);

  r = usys_write(1, 0, 5);
  usys_printf(1, "write(1, NULL, 5) -> %d (expect -1)\n", r);

  r = usys_write(1, msg, -1);
  usys_printf(1, "write(1, msg, -1) -> %d (expect -1)\n", r);
}

static void test_security(void) {
  usys_printf(1, "Testing security (user mode)\n");
  char *bad = (char*)MAXVA; // clearly outside user space
  int r = usys_write(1, bad, 16);
  usys_printf(1, "write(1, bad_ptr, 16) -> %d (expect -1)\n", r);

  char small[4];
  r = usys_read(0, small, 1000);
  usys_printf(1, "read(0, small[4], 1000) -> %d (may clamp/err)\n", r);
}

static void test_syscall_performance(void) {
  usys_printf(1, "Testing syscall performance (user mode)\n");
  uint64_t t0 = uptime();
  for (int i = 0; i < 10000; i++) {
    (void)usys_getpid();
  }
  uint64_t t1 = uptime();
  usys_printf(1, "10000 getpid() ticks=%d\n", (int)(t1 - t0));
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  test_basic_syscalls();
  test_parameter_passing();
  test_security();
  test_syscall_performance();
  usys_exit();
  return 0;
}