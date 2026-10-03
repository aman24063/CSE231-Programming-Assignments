# CSE231 PA2: Adding System Calls to xv6 (RISC-V)

Adds five system calls and a bonus `pstree` utility to xv6-riscv.

## System calls

| Syscall | Number | What it does |
|---|---|---|
| `int getuptime(void)` | 23 | Returns timer ticks since boot, read under `tickslock` |
| `int activecount(void)` | 24 | Counts processes whose state is not `UNUSED` |
| `int lineage(int pid)` | 25 | Prints `PID x: name` from the process up to init; returns the count, or -1 if the PID is not active |
| `int getprocsize(int pid)` | 26 | Returns `p->sz` in bytes, or -1 if the PID is missing or `UNUSED` |
| `int familyheadcount(int)` | 27 | Counts the caller's children that are not `UNUSED` or `ZOMBIE` |
| `int getprocinfo(struct procinfo*, int)` | 28 | Bonus: copies a snapshot of active processes to user space |

Numbers start at 23 because this xv6 tree already uses 22 for `sync`.

## Files changed or added

| File | Change |
|---|---|
| `kernel/syscall.h` | New syscall numbers |
| `kernel/syscall.c` | `extern` declarations and `syscalls[]` entries |
| `kernel/sysproc.c` | Implementations of all six `sys_*` functions |
| `kernel/procinfo.h` | New: `struct procinfo` shared by kernel and `pstree` |
| `user/usys.pl` | User-space stubs |
| `user/user.h` | Prototypes |
| `user/test.c` | New: the provided test program |
| `user/pstree.c` | New: bonus process tree utility |
| `Makefile` | `_test` and `_pstree` added to `UPROGS` |

## Design notes

1. **Locking.** Each `p->lock` is held for one process at a time. `p->parent` is protected by the global `wait_lock`, so `lineage`, `familyheadcount` and `getprocinfo` take `wait_lock` first, then `p->lock`, the same order as `wait()`.
2. **Snapshots.** Values are copied into locals while locked, then the locks are released. In `lineage`, printing happens only after release.
3. **`getprocinfo`.** User space cannot read kernel memory, so the kernel fills a local `struct procinfo` per process and calls `copyout` with no spinlock held. `pstree` builds the tree from the `pid` and `ppid` fields.
4. **`extern`, not `#include`.** Kernel globals (`ticks`, `tickslock`, `proc`, `wait_lock`) are reached with `extern` to avoid multiple-definition link errors.

## This xv6 tree differs from stock xv6

1. The kernel print function is `printk`, not `printf`.
2. `copyout` takes an extra `psz` argument: `copyout(pagetable, psz, dstva, src, len)`.

The code uses these forms. A different xv6 base may need those calls adjusted.

## Build and run

Built with the course Docker image `arghyadipchak/xv6-riscv-devbox:latest`.

    make clean
    make qemu CPUS=3

At the `$` prompt, run `test` and `pstree`. Quit QEMU with Ctrl+A, then X.

## Observed output of `test`

Tick counts and PIDs vary between runs.

    System has been running for 471 ticks
    Number of active processes = 3
    My PID is : 3
    PID 3: test
    PID 2: sh
    PID 1: init
    Count of Ancestors: 3
    Active children before exit: 5
    Active children after exit: 0
    Memory size of process 3: 16384 bytes

## Bonus: `pstree`

Supported flags: none, `-p`, `-m`, `-p -m`, `-pm`, `-mp`.

    $ pstree -p -m
    init(1) [16384B]
    |- sh(2) [20480B]
       |- pstree(7) [16384B]

`pstree` appears under `sh` because `sh` forks it, so this is the real parent-child tree. Sizes depend on the build.

## Known limitation

In `lineage`, the parent pointer is followed after the locks are released. If the parent exits and its slot is reused in that short window, the walk could print a different process. An `UNUSED` slot is detected, but a reused one is not.

## Submission

`73_PA2.zip` is the exact zip submitted on Google Classroom (Group 73).
EOF
