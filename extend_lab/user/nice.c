#include "../kernel/types.h"
#include "user.h"

int main(int argc, char *argv[])
{
  if (argc != 3) {
    printf(2, "Usage: nice pid priority\n");
    exit();
  }
  int pid = atoi(argv[1]);
  int prio = atoi(argv[2]);
  int r = setpriority(pid, prio);
  if (r < 0) {
    printf(2, "nice: failed to set priority pid=%d prio=%d\n", pid, prio);
  }
  exit();
}