#include "user.h"

int main(int argc, char *argv[])
{
  (void)argc; (void)argv;
  // 由内核打印：PID PRIORITY STATE TICKS
  usys_procdump();
  usys_exit();
}
