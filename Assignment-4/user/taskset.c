// Q4: taskset
//   taskset MASK cmd [args...]   run cmd on the harts in MASK (hex, e.g. 0x1)
//   taskset -p MASK PID          change the mask of a running process

#include "kernel/types.h"
#include "user/user.h"

int
hex(char *s)
{
  int v = 0;
  if (s[0] == '0' && s[1] == 'x')
    s += 2;
  for (; *s; s++)
    v = v * 16 + (*s <= '9' ? *s - '0' : *s - 'a' + 10);
  return v;
}

int
main(int argc, char *argv[])
{
  if (argc == 4 && strcmp(argv[1], "-p") == 0) {
    int pid = atoi(argv[3]);
    if (sched_setaffinity(pid, hex(argv[2])) < 0) {
      printf("taskset: failed\n");
      exit(1);
    }
    printf("pid %d: mask 0x%x\n", pid, sched_getaffinity(pid));
    exit(0);
  }
  if (argc < 3) {
    printf("usage: taskset MASK cmd [args...] | taskset -p MASK PID\n");
    exit(1);
  }
  if (sched_setaffinity(0, hex(argv[1])) < 0) {
    printf("taskset: bad mask\n");
    exit(1);
  }
  exec(argv[2], &argv[2]);
  printf("taskset: exec %s failed\n", argv[2]);
  exit(1);
}
