# CSE231 – Programming Assignment 2
## Adding System Calls to xv6 (RISC-V)

This directory contains my implementation of **CSE231 – Operating Systems Programming Assignment 2**. The assignment statement is included as `PA2-syscall-1.pdf`.

The assignment covers extending the **xv6-riscv** kernel with new system calls through:

- **Q1:** `getuptime()`
- **Q2:** `activecount()`
- **Q3:** `lineage(pid)`
- **Q4:** `getprocsize(pid)`
- **Q5:** `familyheadcount(int)`
- **Bonus:** `pstree` process tree visualizer (with the supporting `getprocinfo()` system call)

---

## Objective

The objective is to add new system calls to xv6 and expose kernel information (tick counter, process table, parent links, memory size) to user programs safely, using the kernel's locking rules.

Only the files that were changed or added are included here, with their original relative paths (`kernel/...`, `user/...`, `Makefile`). They are meant to be copied over a clean xv6-riscv tree.

### Life of a System Call

```text
user program: getuptime()
     |
     v
stub generated from user/usys.pl
     |
     +--> li a7, SYS_getuptime
     +--> ecall
     |
     v
uservec (trampoline) --> usertrap() (trap.c)
     |
     v
syscall() (syscall.c)
     |
     +--> read call number from a7
     +--> index syscalls[] table
     +--> call sys_getuptime()
     |
     v
return value stored in a0
     |
     v
user program resumes with the result
```

---

## System Calls

| Syscall | Number | Description |
|---|---|---|
| `int getuptime(void)` | 23 | Returns timer ticks since boot, read under `tickslock` |
| `int activecount(void)` | 24 | Counts processes whose state is not `UNUSED` |
| `int lineage(int pid)` | 25 | Prints `PID x: name` from the process up to init; returns the number printed, or -1 if the PID is not active |
| `int getprocsize(int pid)` | 26 | Returns `p->sz` in bytes, or -1 if the PID is missing or `UNUSED` |
| `int familyheadcount(int)` | 27 | Counts the caller's children that are not `UNUSED` or `ZOMBIE` |
| `int getprocinfo(struct procinfo*, int)` | 28 | Bonus: copies a snapshot of active processes to user space |

Numbering starts at 23 because this xv6 tree already uses 22 for `sync`.

### Q1 – getuptime()

```text
acquire(&tickslock)
     |
     v
copy ticks into a local variable
     |
     v
release(&tickslock)
     |
     v
return the local copy
```

### Q2 – activecount()

```text
for each slot in proc[NPROC]
     |
     +--> acquire(&p->lock)
     +--> if state != UNUSED: count++
     +--> release(&p->lock)
     |
     v
return count
```

### Q3 – lineage(pid)

```text
find process with matching pid (state != UNUSED)
     |
     +--> not found: return -1
     |
     v
loop:
     +--> acquire(&wait_lock), acquire(&cur->lock)
     +--> if UNUSED: stop (broken chain)
     +--> copy pid, name (safestrcpy), parent pointer
     +--> release locks
     +--> print "PID x: name"
     +--> if pid == 1: stop
     +--> cur = parent
     |
     v
return number of lines printed
```

The pid argument is read with `argint`, and the name is copied into a fixed 16-character buffer.

### Q4 – getprocsize(pid)

```text
for each slot in proc[NPROC]
     |
     +--> acquire(&p->lock)
     +--> if pid matches and state != UNUSED:
     |        copy p->sz, release, return it
     +--> release(&p->lock)
     |
     v
return -1
```

### Q5 – familyheadcount()

```text
me = myproc()
acquire(&wait_lock)
     |
     v
for each slot in proc[NPROC]
     +--> acquire(&p->lock)
     +--> if p->parent == me and state is not UNUSED/ZOMBIE: count++
     +--> release(&p->lock)
     |
     v
release(&wait_lock)
return count
```

The `int` argument in the prototype `familyheadcount(int)` is not used. The assignment says the call counts the children of the calling process, so the kernel uses `myproc()`.

---

## Requirements Checklist

| Requirement from the assignment | How it is met |
|---|---|
| Q1: return the existing tick counter, no new state | Reads `ticks` from `trap.c` using `extern` |
| Q1: access the counter under its existing lock | `acquire(&tickslock)` and `release(&tickslock)` |
| Q2: count processes not in `UNUSED` | Loop over `proc[NPROC]` |
| Q2: do not modify any process state, lock each process | Read-only loop with `p->lock` per slot |
| Q3: print ancestry from the process up to PID 1 | Walks the `parent` chain, stops at pid 1 |
| Q3: return the count, or -1 for an inactive PID | Returns `count`, or -1 if the lookup fails |
| Q3: hold at most one process lock at a time | One `p->lock` at a time; the global `wait_lock` is also taken because it protects `parent` |
| Q3: handle a PID that vanishes, do not walk past `UNUSED` | `UNUSED` check inside the walk loop |
| Q3 hints: `safestrcpy`, fixed name length, `argint` | All used |
| Q4: return `p->sz`, or -1 if missing or `UNUSED` | Read under `p->lock` before release |
| Q4: hold at most one process lock | One `p->lock` per iteration |
| Q5: count active children, exclude zombies | `parent == me` and state not `UNUSED` or `ZOMBIE` |
| `extern` for symbols outside `sysproc.c`, no `#include` of `.c` files | `extern` for `ticks`, `tickslock`, `proc`, `wait_lock` |
| Include `test.c` and add it to the Makefile | `user/test.c` added as `$U/_test` in `UPROGS` |
| `test.c` runs after all system calls exist | It calls all five, and its output matches the expected output |
| `test.c` uses the corrected name `familyheadcount` | Verified: `getfamilyheadcount` appears nowhere |
| Bonus: `pstree`, `-p`, `-m`, `-p -m`, `-pm`, `-mp` | All forms tested |
| Clean `make` must build | Verified with `make clean` then `make` |
| Submit one zip, every changed file, paths preserved | `73_PA2.zip` with 9 files, submitted on Google Classroom |

---

## Bonus – pstree

User space cannot read kernel memory, so the kernel copies a snapshot of each process into a user-provided array.

### Execution Flow

```text
pstree (user program)
     |
     v
getprocinfo(buf, NPROC)
     |
     v
kernel: for each active process
     +--> lock, copy pid / ppid / sz / name into a local struct
     +--> release locks
     +--> copyout() to the user buffer
     |
     v
pstree: find pid 1, then print children recursively
        (children of X = entries whose ppid == X.pid)
```

### Supported Flags

```text
pstree          names only
pstree -p       names with PIDs
pstree -m       names with memory size
pstree -p -m    names with PIDs and memory size
pstree -pm      same as -p -m
pstree -mp      same as -p -m
```

---

## Directory Structure

```text
Assignment-2/
├── Makefile
├── kernel/
│   ├── syscall.h
│   ├── syscall.c
│   ├── sysproc.c
│   └── procinfo.h
├── user/
│   ├── usys.pl
│   ├── user.h
│   ├── test.c
│   └── pstree.c
├── PA2-syscall-1.pdf
└── Readme.MD
```

### Files Changed or Added

| File | Change |
|---|---|
| `kernel/syscall.h` | New syscall numbers |
| `kernel/syscall.c` | `extern` declarations and `syscalls[]` entries |
| `kernel/sysproc.c` | Implementations of all six `sys_*` functions |
| `kernel/procinfo.h` | New: `struct procinfo` shared by kernel and `pstree` |
| `user/usys.pl` | User-space stubs |
| `user/user.h` | Prototypes |
| `user/test.c` | New: the provided test program |
| `user/pstree.c` | New: bonus utility |
| `Makefile` | `_test` and `_pstree` added to `UPROGS` |
| `PA2-syscall-1.pdf` | The assignment statement |

---

## Build

The code was built using the course Docker image `arghyadipchak/xv6-riscv-devbox:latest`.

```bash
git clone https://github.com/mit-pdos/xv6-riscv.git
cd xv6-riscv
# copy the files from Assignment-2/ over the tree, keeping paths
make clean
make qemu CPUS=3
```

### Notes About This xv6 Tree

This tree differs from stock xv6, and the code uses these forms:

```text
printk(...)                                   instead of printf(...) in the kernel
copyout(pagetable, psz, dstva, src, len)      one extra psz argument
```

A different xv6 base may need those calls adjusted.

---

## Run

At the `$` prompt:

```text
test
pstree
pstree -p
pstree -m
pstree -p -m
```

Quit QEMU with `Ctrl+A`, then `X`.

### Observed Output of test

Tick counts and PIDs vary between runs.

```text
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
```

### Observed Output of pstree

```text
$ pstree -p -m
init(1) [16384B]
|- sh(2) [20480B]
   |- pstree(7) [16384B]
```

### Differences From the Example in the Assignment

1. The assignment's example shows `sh` and `pstree` side by side under `init`. Here `pstree` appears under `sh`, because `sh` forks it, so this is the real parent-child tree.
2. The memory sizes and PIDs in the assignment's example are illustrative. Real values depend on the build and the run.

---

## Locking Rules Used

```text
p->lock      protects state, pid, sz, name of one process
wait_lock    protects p->parent (global lock)

Order:  wait_lock first, then p->lock   (same as wait())
Rule:   never call copyout() or printk() while holding a spinlock
```

---

## Technologies Used

- C
- xv6-riscv
- RISC-V
- QEMU
- Docker
- GNU Make
- Perl (`usys.pl`)
- Git

---

## Key Concepts

- System call path: `ecall`, trap, `syscall()`, dispatch table
- Registers `a0` to `a7` for arguments and call number
- Process table and `struct proc`
- Process states (`UNUSED`, `ZOMBIE`, and others)
- Spinlocks and lock ordering
- `extern` versus `#include` for kernel globals
- `copyout()` for kernel-to-user data transfer
- Parent-child relationships and zombie processes
- Makefile `UPROGS`

---

## Known Limitation

In `lineage`, the parent pointer is followed after the locks are released. If the parent exits and its slot is reused in that short window, the walk could print a different process. An `UNUSED` slot is detected, but a reused one is not.

---

## Overall Learning

```text
user stub (usys.pl)
      |
      v
ecall --> trap --> syscall()
      |
      v
syscalls[] table --> sys_xxx()
      |
      v
lock, read kernel data, unlock
      |
      v
return value in a0 --> user program
```

---

