# CSE231 Assignment 4: Scheduling in xv6

This assignment adds scheduling system calls to [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) (`riscv` branch). It adds per-process scheduling stats, configurable time slices, first-run tracing, CPU affinity, a strict three-level priority scheduler, and a `top`-style live monitor (`xtop`).

This folder holds only the files that were changed or added. Copy them over a fresh xv6-riscv clone to build.

---

## Folder layout

```
Assignment-4/
├── kernel/                     modified xv6 kernel files (the solution)
├── user/                       user-side changes, test programs and xtop
├── Makefile                    xv6 Makefile with the new programs added
├── PA4-scheduling.docx         assignment problem statement
├── PA4_handout-20261112T022716Z-1-001/
│   └── PA4_handout/            original handout: schedinfo.h and the 6 test programs, unmodified
└── Readme.MD                   this file
```

Only `kernel/`, `user/` and `Makefile` are needed to build. The handout folder is kept for reference, and its files are already included in `kernel/` and `user/`.

---

## Contents

| Question | Feature | System calls | Test program |
| --- | --- | --- | --- |
| Q1 | Per-process scheduling stats | `getcpu()`, `schedinfo(pid, *si)` | `whereami` |
| Q2 | Configurable time slices | `setslice(pid, n)` | `slicetest` |
| Q3 | First-run tracing | `forkwatch(on)` | `forkrace` |
| Q4 | CPU affinity | `sched_setaffinity(pid, mask)`, `sched_getaffinity(pid)` | `taskset`, `spin` |
| Q5 | Priority scheduler | `schedpolicy(policy)`, `setprio(pid, prio)` | `priotest` |
| Bonus | Live process monitor | `getpids(*pids, max)` | `xtop` |

## Files changed

```
Makefile              added the 7 user programs to UPROGS
kernel/proc.h         new fields in struct proc
kernel/proc.c         scheduler (RR + PRIO), kfork/forkret/allocproc changes, syscall logic
kernel/trap.c         timer path calls timer_should_yield() instead of always yield()
kernel/main.c         each hart records itself in booted_harts
kernel/defs.h         prototypes
kernel/syscall.h      syscall numbers 23-31
kernel/syscall.c      syscall table entries
kernel/sysproc.c      sys_* wrappers (argument parsing + copyout)
kernel/schedinfo.h    shared struct schedinfo (from handout)
user/user.h           user-side declarations
user/usys.pl          user-side syscall stubs
user/xtop.c           bonus monitor
user/whereami.c  user/slicetest.c  user/forkrace.c
user/taskset.c   user/spin.c       user/priotest.c      (handout tests)
```

---

## How to build and run

xv6 needs a RISC-V cross-compiler and **QEMU ≥ 7.2**. Ubuntu 22.04's `apt` only ships QEMU 6.2, so the course Docker devbox is used.

```bash
# 1. get this repo, then a fresh xv6, and copy this assignment on top
git clone https://github.com/aman24063/CSE231-Programming-Assignments.git
git clone https://github.com/mit-pdos/xv6-riscv.git pa4-demo
cp -r CSE231-Programming-Assignments/Assignment-4/kernel \
      CSE231-Programming-Assignments/Assignment-4/user \
      CSE231-Programming-Assignments/Assignment-4/Makefile pa4-demo/

# 2. open the devbox with the folder mounted
docker run -it --rm -v ~/pa4-demo:/xv6 -w /xv6 arghyadipchak/xv6-riscv-devbox:latest bash

# 3. inside the container
make clean
make qemu            # 3 harts (default)
make CPUS=1 qemu     # single hart
```

Quit QEMU with **Ctrl-A, then X**.

### Tests

```
$ whereami                    # Q1
$ slicetest                   # Q2
$ forkrace                    # Q3
$ spin &                      # Q4: note its pid P
$ taskset -p 0x1 P            #     now prints "on hart 0" only
$ taskset -p 0x8 P            #     fails: hart 3 never booted
$ kill P
$ taskset 0x1 spin &          #     mask survives exec
$ priotest                    # Q5
$ xtop -n 3                   # Bonus
$ taskset 0x2 usertests -q    # must print ALL TESTS PASSED
```

`usertests -q` must also pass under the priority policy. No shell command switches the policy, so to check it, change `static int sched_policy = POLICY_RR;` to `POLICY_PRIO` in `kernel/proc.c`, rebuild with `make clean && make qemu`, run `usertests -q`, then change it back.

### Expected results

| Test | What it shows |
| --- | --- |
| `whereami` | each child: `nsched` ≈ `rticks` ≈ 30; works on zombies |
| `slicetest` | ticks per turn ≈ 1 for slice 1, ≈ 10 for slice 10 |
| `forkrace` | `waited 0` with 3 harts; `waited ≥ 1` with `CPUS=1` |
| `taskset -p` | process moves to the new hart within one tick |
| `priotest` | RR: ~equal split. PRIO: HIGH ≈ 50 ticks, others ≈ 0 |
| `xtop` | live table of PID, NAME, STATE, PRIO, CPU, MASK, SLICE, %CPU |

---

## Design

### One timer hook for everything
Stock xv6 calls `yield()` on every timer interrupt. Both `usertrap()` and `kerneltrap()` now call `timer_should_yield()`, which, under `p->lock`:

1. increments `rticks` (Q1) and `used` (Q2);
2. yields if `used >= slice` (Q2);
3. yields if the affinity mask no longer includes this hart (Q4, so the process moves within one tick);
4. under PRIO, after releasing its own lock, yields if a better-priority process is RUNNABLE on this hart (Q5).

### Q1: `getcpu`, `schedinfo`
- `getcpu` wraps `cpuid()` in `push_off()` / `pop_off()`, so a timer interrupt cannot move the process mid-read.
- `nsched` and `lastcpu` are updated in `run_proc()`, the single place both policies mark a process RUNNING.
- `schedinfo` copies fields into a kernel-local struct under `p->lock`, releases it, **then** calls `copyout` (copyout can page-fault, so it must not run under a spinlock). Any non-UNUSED state matches, including ZOMBIE.

### Q2: `setslice`
- Default `slice = 1` keeps the original behaviour.
- `used` is reset to 0 each time the scheduler switches the process in.
- In `kfork`, the parent's slice/affinity/priority are read under the **parent's** lock and copied to locals, and that lock is released **before** `allocproc()` takes the child's lock. At most one process lock is held at a time.

### Q3: `forkwatch`
- `ctime = ticks` is set where `kfork` makes the child RUNNABLE. `ticks` is read without `tickslock`, because `clockintr` takes `tickslock` and then process locks; taking them in reverse could deadlock.
- `ftime` / `firstcpu` are set at the top of `forkret` (the one function every new process runs exactly once), and `printk` runs only after `p->lock` is released.

### Q4: affinity
- Each hart sets its bit in `booted_harts` with `__atomic_fetch_or` in `main()`.
- init is created before harts 1–2 boot, so the stored default is "all harts" and the mask is ANDed with `booted_harts` whenever it is reported (shows as `0x7` with 3 harts).
- Both schedulers skip processes whose mask excludes the current hart. A process that excludes its own hart calls `yield()` inside the syscall. The mask is untouched by `exec`, so `taskset 0x1 sh` pins everything it starts.

### Q5: priority scheduler
`pick_prio()` works in two passes and never holds more than one `p->lock` at a time:
1. Find the best priority level that has a RUNNABLE process allowed on this hart.
2. Round-robin within that level from a per-level cursor (`rr_cursor[level]`), so ties do not always favour `proc[0]`. Lock each candidate and re-check that it is still RUNNABLE, at that level, and allowed. If no candidate passes, scan again.

If a process lowers its own priority below a RUNNABLE process, `setprio` yields immediately instead of waiting for the next tick. Children inherit priority.

### Bonus: `xtop`
- `getpids` gathers pids into a kernel buffer under locks, then copies them out with no lock held.
- `xtop` takes a baseline sample before the first frame, so even `-n 1` shows real `%CPU` (Δ`rticks` / Δ`uptime`).
- A process that exits between `getpids` and `schedinfo` is skipped.
- Each frame goes out in a single `write()` to avoid flicker. Options: `-d N` (refresh every N ticks), `-n F` (stop after F frames).

---

## Locking rules followed
- Scheduling fields are read and written only while holding `p->lock`.
- Never more than one process lock at a time (`kfork`, scheduler scans, `better_runnable`).
- Only `p->lock` is held across `swtch()`.
- No `copyout` or `printk` while holding a process lock.
