#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "procinfo.h"

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

extern struct spinlock tickslock;
extern uint ticks;

uint64
sys_getuptime(void)
{
  uint t;
  acquire(&tickslock);
  t = ticks;
  release(&tickslock);
  return t;
}

extern struct proc proc[NPROC];

uint64
sys_activecount(void)
{
  struct proc *p;
  int count = 0;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED)
      count++;
    release(&p->lock);
  }
  return count;
}

extern struct spinlock wait_lock;

uint64
sys_lineage(void)
{
  int pid;
  argint(0, &pid);

  struct proc *p, *cur = 0;
  char name[16];
  int count = 0;

  // find the starting process
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && p->pid == pid){
      cur = p;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }
  if(cur == 0)
    return -1;

  // walk up the parent chain, one process at a time
  while(cur){
    int cpid;
    struct proc *parent;

    acquire(&wait_lock);
    acquire(&cur->lock);
    if(cur->state == UNUSED){      // chain broke mid-walk
      release(&cur->lock);
      release(&wait_lock);
      break;
    }
    cpid = cur->pid;
    safestrcpy(name, cur->name, sizeof(name));
    parent = cur->parent;
    release(&cur->lock);
    release(&wait_lock);

    printk("PID %d: %s\n", cpid, name);
    count++;
    if(cpid == 1)
      break;
    cur = parent;
  }
  return count;
}

uint64
sys_getprocsize(void)
{
  int pid;
  argint(0, &pid);

  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      uint64 sz = p->sz;
      release(&p->lock);
      return sz;
    }
    release(&p->lock);
  }
  return -1;
}

uint64
sys_familyheadcount(void)
{
  struct proc *me = myproc();
  struct proc *p;
  int count = 0;

  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->parent == me && p->state != UNUSED && p->state != ZOMBIE)
      count++;
    release(&p->lock);
  }
  release(&wait_lock);
  return count;
}

uint64
sys_getprocinfo(void)
{
  uint64 addr;
  int max, n = 0;
  argaddr(0, &addr);
  argint(1, &max);

  struct proc *p;
  for(p = proc; p < &proc[NPROC] && n < max; p++){
    struct procinfo info;
    acquire(&wait_lock);
    acquire(&p->lock);
    if(p->state == UNUSED){
      release(&p->lock);
      release(&wait_lock);
      continue;
    }
    info.pid  = p->pid;
    info.ppid = p->parent ? p->parent->pid : 0;
    info.sz   = p->sz;
    safestrcpy(info.name, p->name, sizeof(info.name));
    release(&p->lock);
    release(&wait_lock);

    if(copyout(myproc()->pagetable, myproc()->sz, addr + n * sizeof(info),
               (char*)&info, sizeof(info)) < 0)
      return -1;
    n++;
  }
  return n;
}
