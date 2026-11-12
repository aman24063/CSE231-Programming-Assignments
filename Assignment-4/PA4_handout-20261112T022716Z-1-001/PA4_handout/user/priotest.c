// Q5: priotest
// Three CPU hogs (HIGH, NORMAL, LOW) are pinned to hart 0 for 50 ticks,
// first under round-robin, then under the priority policy.
// RR: each gets about a third.  PRIO: HIGH gets almost everything.

#include "kernel/types.h"
#include "kernel/schedinfo.h"
#include "user/user.h"

char *names[3] = { "HIGH", "NORMAL", "LOW" };

void
run(int policy)
{
  int pids[3];
  struct schedinfo si;
  int end = uptime() + 50;

  schedpolicy(policy);
  for (int i = 0; i < 3; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      sched_setaffinity(0, 0x1); // all three compete for hart 0
      setprio(0, i);
      while (uptime() < end)
        ;
      exit(0);
    }
  }

  printf("%s:\n", policy == POLICY_RR ? "round-robin" : "priority");
  for (int i = 0; i < 3; i++) {
    while (schedinfo(pids[i], &si) == 0 && si.state != PS_ZOMBIE)
      pause(1);
    printf("  %s: rticks %d\n", names[si.prio], si.rticks);
  }
  for (int i = 0; i < 3; i++)
    wait(0);
}

int
main(void)
{
  int old = schedpolicy(-1);
  setprio(0, PRIO_HIGH); // so we finish forking before the HIGH child runs
  run(POLICY_RR);
  run(POLICY_PRIO);
  schedpolicy(old);
  exit(0);
}
