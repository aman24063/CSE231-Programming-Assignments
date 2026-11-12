# CSE231 PA3: handout files

Base code: https://github.com/mit-pdos/xv6-riscv (riscv branch)

## Files

| File | Used in |
| --- | --- |
| kernel/schedinfo.h | All questions: `struct schedinfo` and constants. Include it in kernel files that need it. |
| user/whereami.c | Q1 |
| user/slicetest.c | Q2 |
| user/forkrace.c | Q3 |
| user/taskset.c, user/spin.c | Q4 |
| user/priotest.c | Q5 |

## Setup

1. Copy `kernel/` and `user/` from this folder into your xv6-riscv tree.
2. Add each test program to `UPROGS` in the Makefile once its system calls exist:

```
	$U/_whereami\
	$U/_slicetest\
	$U/_forkrace\
	$U/_taskset\
	$U/_spin\
	$U/_priotest\
```

3. The test programs expect these declarations in `user/user.h`:

```c
struct schedinfo;
int getcpu(void);
int schedinfo(int, struct schedinfo *);
int setslice(int, int);
int forkwatch(int);
int sched_setaffinity(int, int);
int sched_getaffinity(int);
int schedpolicy(int);
int setprio(int, int);
```

The bonus `getpids` and `xtop` are yours to write.
