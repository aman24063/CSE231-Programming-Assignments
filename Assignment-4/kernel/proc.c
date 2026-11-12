#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "schedinfo.h"

#define NPRIO   3                    // number of priority levels
#define AFF_ALL ((1 << NCPU) - 1)    // default mask: "every hart"

// Q4: bit i is set once hart i has passed through main().
int booted_harts;

// Q3: non-zero => print a line when a new process first runs.
static int forkwatch_on;

// Q5: global policy, and one round-robin cursor per priority level
// (index of the proc[] slot most recently picked at that level).
static int sched_policy = POLICY_RR;
static int rr_cursor[NPRIO] = { NPROC - 1, NPROC - 1, NPROC - 1 };

// cpuid() is only meaningful with interrupts off.
static int
curhart(void)
{
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // PA4 defaults (we hold p->lock here).
  p->nsched = 0;
  p->rticks = 0;
  p->lastcpu = 0;
  p->slice = 1; // 1 tick == original xv6 behaviour
  p->used = 0;
  p->ctime = 0;
  p->ftime = 0;
  p->firstcpu = 0;
  p->affinity = AFF_ALL; // masked with booted_harts whenever reported
  p->prio = PRIO_NORMAL;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->ctime = ticks;
  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Snapshot the parent's scheduling settings under the parent's lock,
  // and drop it BEFORE allocproc() takes np->lock: at most one
  // process lock is ever held at a time.
  acquire(&p->lock);
  int pslice = p->slice;
  int paff = p->affinity;
  int pprio = p->prio;
  release(&p->lock);

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  // Child inherits slice, affinity and priority (np->lock is held).
  np->slice = pslice;
  np->affinity = paff;
  np->prio = pprio;

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->ctime = ticks; // Q3: plain read; taking tickslock here would invert
                     // the tickslock -> p->lock order used by clockintr()
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          pp->parent = 0;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep_prepare(p); //DOC: wait-sleep
    release(&wait_lock);
    sleep();
    acquire(&wait_lock);
  }
}

// Switch this hart to process p.  Caller holds p->lock and p is RUNNABLE.
// Returns (still holding p->lock) once p gives the CPU back.
static void
run_proc(struct cpu *c, struct proc *p, int id)
{
  p->state = RUNNING;
  p->nsched++;    // Q1
  p->lastcpu = id; // Q1
  p->used = 0;    // Q2: fresh quantum
  c->proc = p;
  swtch(&c->context, &p->context);

  // Don't re-enable interrupts on release.
  mycpu()->intena = 0;

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

// Q5: choose the next process for hart id under POLICY_PRIO.
// Returns the winner with its lock held, or 0 if nothing can run here.
// Only one p->lock is held at any moment while scanning.
static struct proc *
pick_prio(int id)
{
  struct proc *p;

  for (;;) {
    // Pass 1: which is the best (numerically lowest) level that has
    // a RUNNABLE process allowed on this hart?
    int best = NPRIO;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE && (p->affinity & (1 << id)) && p->prio < best)
        best = p->prio;
      release(&p->lock);
    }
    if (best == NPRIO)
      return 0;

    // Pass 2: round-robin within that level, starting just after the
    // slot picked last time, so ties rotate instead of favouring proc[0].
    int start = __atomic_load_n(&rr_cursor[best], __ATOMIC_RELAXED);
    for (int k = 1; k <= NPROC; k++) {
      int i = (start + k) % NPROC;
      p = &proc[i];
      acquire(&p->lock);
      // Re-check under the lock: another hart may have taken it,
      // or its priority / mask may have changed since pass 1.
      if (p->state == RUNNABLE && (p->affinity & (1 << id)) && p->prio == best) {
        __atomic_store_n(&rr_cursor[best], i, __ATOMIC_RELAXED);
        return p;
      }
      release(&p->lock);
    }
    // The candidate disappeared between the passes: scan again.
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int id = cpuid(); // a scheduler thread never changes hart

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    if (__atomic_load_n(&sched_policy, __ATOMIC_RELAXED) == POLICY_PRIO) {
      p = pick_prio(id);
      if (p) {
        run_proc(c, p, id);
        release(&p->lock);
        found = 1;
      }
    } else {
      // Original round-robin, but skipping processes whose
      // affinity mask excludes this hart (Q4).
      for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == RUNNABLE && (p->affinity & (1 << id))) {
          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.
          run_proc(c, p, id);
          found = 1;
        }
        release(&p->lock);
      }
    }
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Q3: first time this process ever runs.  Still holding p->lock from
  // scheduler() (and so interrupts are off, cpuid() is safe).
  p->ftime = ticks;
  p->firstcpu = cpuid();
  int pid = p->pid, waited = p->ftime - p->ctime, hart = p->firstcpu;

  // Still holding p->lock from scheduler.
  release(&p->lock);

  // printk only after the lock is released (printk takes its own lock
  // and can be slow; never do that while holding a process lock).
  if (__atomic_load_n(&forkwatch_on, __ATOMIC_RELAXED))
    printk("pid %d: first run on hart %d, %d ticks after fork\n", pid, hart, waited);

  if (first) {
    first = 0;

    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Register current process as waiting for wakeups on chan.
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (chan == 0)
    panic("sleep_prepare: zero chan");
  p->chan = chan;
  release(&p->lock);
}

// Put the thread to sleep.  Assumes sleep_prepare() was called before.
// If the channel registered by sleep_prepare() has been woken up in
// the meantime, do not go to sleep, and instead return immediately.
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (p->chan != 0) {
    p->state = SLEEPING;
    sched();
  }
  release(&p->lock);
}

// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->chan == chan) {
      // If the process is waiting for wakeups on this channel,
      // signal that the wakeup happened by clearing p->chan.
      p->chan = 0;

      // If this waiting process has gotten so far as to actually
      // go to sleep, also set it back to RUNNING.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
      }
    }
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  if (pid == 0)
    return -1;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    // clang-format off
    [UNUSED]    = "unused",
    [USED]      = "used",
    [SLEEPING]  = "sleep ",
    [RUNNABLE]  = "runble",
    [RUNNING]   = "run   ",
    [ZOMBIE]    = "zombie"
    // clang-format on
  };
  struct proc *p;
  char *state;

  printk("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printk("%d %s %s", p->pid, state, p->name);
    printk("\n");
  }
}

// ---------------------------------------------------------------------
// PA4: scheduling system calls (kernel side)
// ---------------------------------------------------------------------

// pid 0 means "the caller".
static int
resolve_pid(int pid)
{
  return pid == 0 ? myproc()->pid : pid;
}

static int
booted(void)
{
  return __atomic_load_n(&booted_harts, __ATOMIC_ACQUIRE);
}

// Is there a RUNNABLE process with priority strictly better than prio
// that is allowed on hart id?  Caller must hold NO process lock.
static int
better_runnable(int prio, int id)
{
  struct proc *p;
  int found = 0;

  for (p = proc; p < &proc[NPROC] && !found; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE && p->prio < prio && (p->affinity & (1 << id)))
      found = 1;
    release(&p->lock);
  }
  return found;
}

// Called on every timer interrupt that arrives while myproc() is RUNNING
// (from usertrap() and kerneltrap()).  Does the Q1/Q2 accounting and says
// whether the process should give up the CPU now.
int
timer_should_yield(void)
{
  struct proc *p = myproc();
  int id = curhart();
  int y, prio;

  acquire(&p->lock);
  p->rticks++;                         // Q1
  p->used++;                           // Q2
  y = p->used >= p->slice;             // Q2: quantum used up
  if (!(p->affinity & (1 << id)))      // Q4: mask changed under us
    y = 1;
  prio = p->prio;
  release(&p->lock);

  // Q5: under PRIO, a better process that became RUNNABLE (e.g. woke up)
  // preempts us at the next tick even if our slice is not over.
  if (!y && __atomic_load_n(&sched_policy, __ATOMIC_RELAXED) == POLICY_PRIO &&
      better_runnable(prio, id))
    y = 1;
  return y;
}

// Q1: fill *si for process pid.  Works for any non-UNUSED state,
// including ZOMBIE.  Returns 0, or -1 if there is no such process.
int
kschedinfo(int pid, struct schedinfo *si)
{
  struct proc *p;

  pid = resolve_pid(pid);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      si->pid = p->pid;
      si->state = p->state; // enum procstate matches PS_*
      si->nsched = p->nsched;
      si->rticks = p->rticks;
      si->lastcpu = p->lastcpu;
      si->slice = p->slice;
      si->ctime = p->ctime;
      si->ftime = p->ftime;
      si->firstcpu = p->firstcpu;
      si->affinity = p->affinity & booted();
      si->prio = p->prio;
      safestrcpy(si->name, p->name, sizeof(si->name));
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Q2
int
ksetslice(int pid, int n)
{
  struct proc *p;

  if (n < 1 || n > 100)
    return -1;
  pid = resolve_pid(pid);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      p->slice = n;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Q3: returns the previous setting.
int
kforkwatch(int on)
{
  return __atomic_exchange_n(&forkwatch_on, on != 0, __ATOMIC_RELAXED);
}

// Q4
int
ksetaffinity(int pid, int mask)
{
  struct proc *p;

  mask &= booted(); // keep only harts that actually exist
  if (mask == 0)
    return -1;
  pid = resolve_pid(pid);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      p->affinity = mask;
      release(&p->lock);
      // If we just banned our own hart, leave it before returning.
      // yield() -> scheduler on this hart will skip us, so we resume
      // only when an allowed hart picks us up.  (A process running on
      // some other hart is moved by timer_should_yield() instead.)
      if (p == myproc() && !(mask & (1 << curhart())))
        yield();
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

int
kgetaffinity(int pid)
{
  struct proc *p;

  pid = resolve_pid(pid);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      int m = p->affinity & booted();
      release(&p->lock);
      return m;
    }
    release(&p->lock);
  }
  return -1;
}

// Q5: -1 queries; 0/1 sets and returns the old policy.
int
kschedpolicy(int policy)
{
  if (policy == -1)
    return __atomic_load_n(&sched_policy, __ATOMIC_RELAXED);
  if (policy != POLICY_RR && policy != POLICY_PRIO)
    return -1;
  return __atomic_exchange_n(&sched_policy, policy, __ATOMIC_RELAXED);
}

int
ksetprio(int pid, int prio)
{
  struct proc *p;

  if (prio < PRIO_HIGH || prio > PRIO_LOW)
    return -1;
  pid = resolve_pid(pid);
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pid == pid) {
      p->prio = prio;
      release(&p->lock);
      // If the caller just made itself worse than something RUNNABLE
      // on this hart, switch now rather than at the next tick.
      if (p == myproc() &&
          __atomic_load_n(&sched_policy, __ATOMIC_RELAXED) == POLICY_PRIO &&
          better_runnable(prio, curhart()))
        yield();
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Bonus: copy up to max pids of non-UNUSED processes into kpids.
int
kgetpids(int *kpids, int max)
{
  struct proc *p;
  int n = 0;

  for (p = proc; p < &proc[NPROC] && n < max; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED)
      kpids[n++] = p->pid;
    release(&p->lock);
  }
  return n;
}
