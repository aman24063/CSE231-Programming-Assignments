// Q4: spin
// A CPU hog. Every 10 ticks it prints the hart it is running on.

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int next = uptime() + 10;
  for (;;) {
    if (uptime() >= next) {
      printf("spin %d: on hart %d\n", getpid(), getcpu());
      next += 10;
    }
  }
}
