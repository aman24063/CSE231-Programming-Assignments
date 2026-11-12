#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "schedinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// ---------------- PA4 ----------------

// Q1: which hart am I on?  cpuid() needs interrupts off, otherwise a
// timer interrupt could move us to another hart mid-read.
uint64
sys_getcpu(void)
{
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}

// Q1: kschedinfo() copies the fields into a kernel-local struct while
// holding p->lock; only after it has released the lock do we copyout()
// (copyout can fault / sleep, so it must never run under a spinlock).
uint64
sys_schedinfo(void)
{
  int pid;
  uint64 addr;
  struct schedinfo si;
  struct proc *p = myproc();

  argint(0, &pid);
  argaddr(1, &addr);
  memset(&si, 0, sizeof(si));
  if (kschedinfo(pid, &si) < 0)
    return -1;
  if (copyout(p->pagetable, p->sz, addr, (char *)&si, sizeof(si)) < 0)
    return -1;
  return 0;
}

uint64
sys_setslice(void)
{
  int pid, n;
  argint(0, &pid);
  argint(1, &n);
  return ksetslice(pid, n);
}

uint64
sys_forkwatch(void)
{
  int on;
  argint(0, &on);
  return kforkwatch(on);
}

uint64
sys_sched_setaffinity(void)
{
  int pid, mask;
  argint(0, &pid);
  argint(1, &mask);
  return ksetaffinity(pid, mask);
}

uint64
sys_sched_getaffinity(void)
{
  int pid;
  argint(0, &pid);
  return kgetaffinity(pid);
}

uint64
sys_schedpolicy(void)
{
  int policy;
  argint(0, &policy);
  return kschedpolicy(policy);
}

uint64
sys_setprio(void)
{
  int pid, prio;
  argint(0, &pid);
  argint(1, &prio);
  return ksetprio(pid, prio);
}

// Bonus: same pattern as schedinfo -- gather into a kernel buffer under
// the locks, then copyout with no lock held.
uint64
sys_getpids(void)
{
  uint64 addr;
  int max, n;
  int kpids[NPROC];
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &max);
  if (max < 0)
    return -1;
  if (max > NPROC)
    max = NPROC;
  n = kgetpids(kpids, max);
  if (n > 0 && copyout(p->pagetable, p->sz, addr, (char *)kpids, n * sizeof(int)) < 0)
    return -1;
  return n;
}
