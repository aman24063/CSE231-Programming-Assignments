# CSE231 PA3: A Shell and a Threaded Print Service


This assignment has three parts:

| Part | What it is | Concepts |
|------|-----------|----------|
| **A** | `msh`, a mini Unix shell | `fork()`, `execvp()`, `waitpid()`, built-ins (`cd`, `exit`) |
| **B** | A multithreaded counter that exposes a race condition | `pthread_create()`, `pthread_join()`, lost updates |
| **C** | The counter fixed with a mutex, then a bounded producer-consumer **print server** | `pthread_mutex_*`, `pthread_cond_*`, circular queue, graceful shutdown |
| **Bonus** | Several producer threads feeding the same queue at the same time | Ordering shutdown across producers and consumers |

Parts B and C tell one story, from shared state to a race, then a mutex, then a bounded queue, then condition variables, and finally a graceful shutdown.

---

## Directory structure

```
Assignment-3/
├── README.md                     this file
├── 73_PA3.zip                    submitted zip (group 73)
└── PA-3/
    └── PA3-shell-concurrency/    ← the project; run every command from here
        ├── Makefile              builds all three programs
        ├── answers.txt           answers to the reasoning questions
        ├── README.txt            original starter-code readme
        ├── PA3-shell-concurrency.docx   assignment handout
        ├── A/
        │   └── minishell.c       Part A: the shell
        ├── B/
        │   └── thread_counter.c  Part B (unsafe counter) + Part C1/C2 (mutex fix)
        └── C/
            ├── job_queue.h       bounded queue API
            ├── job_queue.c       bounded queue (mutex + 2 condition variables)
            └── print_server.c    print server: 4 printer threads + core / bonus modes
```

## Building

All commands in this README are run from the project folder:

```bash
cd PA-3/PA3-shell-concurrency
make          # builds A/msh, B/thread_counter, C/print_server
make clean    # removes the binaries
```

The code compiles with `-std=c11 -Wall -Wextra -Werror` without warnings.

---

## Part A: Mini shell (`A/msh`)

```bash
./A/msh
```

```
msh> /bin/echo hello world
hello world
msh> cd /tmp
msh> /bin/pwd
/tmp
msh> command_that_does_not_exist
msh: command_that_does_not_exist: No such file or directory
msh> /bin/echo still-alive
still-alive
msh> exit
```

### How it works

1. **External commands:** the shell calls `fork()`. The **child** calls `execvp(args[0], args)`, which replaces the child's program image with the command. The **parent** calls `waitpid(pid, ...)` to wait for that specific child, then shows the prompt again.
2. **A failed `execvp()`** ends only the child, with `_exit(127)`. The shell keeps running. We use `_exit()` rather than `exit()` so the child does not flush its copy of the parent's stdio buffers, which would print output twice. 127 is the usual "command not found" status.
3. **`waitpid()` is retried on `EINTR`**, so a signal arriving during the wait does not lose track of the child.
4. **`cd <dir>`** is a built-in that runs in the shell process itself, using `chdir()`. The working directory belongs to each process separately, so a `cd` in a child would vanish when the child exited. The shell requires exactly one argument and calls `perror("cd")` if `chdir()` fails.
5. **`exit`** is a built-in that ends the shell immediately. Pressing Ctrl-D (end of input) also exits.

Out of scope (as the handout says): pipes, redirection, quoting, variable expansion, globbing, job control and history. The code never uses `system()` or `popen()`.

---

## Part B / C1: Thread counter (`B/thread_counter`)

```bash
./B/thread_counter [unsafe|safe] [threads] [iterations]
./B/thread_counter unsafe 4 100000   # race: Actual is usually < Expected
./B/thread_counter safe   4 100000   # mutex: Actual == Expected every time
```

- Each worker runs `total_jobs++` once per iteration.
- In **unsafe** mode there is no lock. `total_jobs++` is really three steps (load, add, store), so two threads can interleave between them and lose updates.
- In **safe** mode, `counter_mutex` makes each increment a critical section, so only one thread can do it at a time.
- Each thread gets a pointer to its own `args[i]`. That array lives until every thread has been joined, so the pointer stays valid. All `pthread_create`/`pthread_join` return codes are checked. These functions return an error number; they don't set `errno`.

Sample results from 10 unsafe runs with 4 × 100000:

```
300523 311528 303998 302643 400000 215341 301655 300000 205286 317903
```

One run printed the "correct" 400000 even though the race was there. A correct-looking output does not prove the code is correct (see B-Q3).

---

## Part C: Bounded job queue (`C/job_queue.c`)

A circular buffer with capacity 8, protected by **one mutex** and **two condition variables**:

| Field / CV | Role |
|------------|------|
| `head` | index of the next job to pop |
| `tail` | index of the next free slot |
| `count` | number of stored jobs. Needed because `head == tail` happens both when the queue is empty and when it is full |
| `shutdown` | set to 1 when no future jobs will arrive |
| `mutex` | protects **all** of the fields above |
| `not_empty` | consumers sleep here while the queue is empty |
| `not_full` | producers sleep here while the queue is full |

Every operation follows the same pattern:

```c
pthread_mutex_lock(&q->mutex);
while (/* cannot proceed */)
    pthread_cond_wait(&cv, &q->mutex);   // atomically unlock + sleep, relock on wake
/* read/modify head, tail, count */
pthread_cond_signal(&other_cv);           // wake the other side
pthread_mutex_unlock(&q->mutex);
```

| Function | Behaviour |
|----------|-----------|
| `queue_init` | Sets up the indices, the mutex and both CVs. If a step fails, it undoes the earlier ones and returns −1 |
| `queue_push` | Waits **while** the queue is full and not shut down. Returns −1 if shut down. Otherwise inserts at `tail`, signals `not_empty`, returns 0 |
| `queue_pop` | Waits **while** the queue is empty and not shut down. Returns 0 only if it is **empty and shut down**. Otherwise removes from `head`, signals `not_full`, returns 1 |
| `queue_shutdown` | Sets `shutdown = 1` under the mutex and **broadcasts** `not_empty`, so every sleeping consumer wakes up and exits. It also broadcasts `not_full`, a defensive extra that does nothing when shutdown happens in the correct order |
| `queue_destroy` | Destroys the mutex and CVs. Called only after every thread has been joined |

Key design points:

- **`while`, not `if`, around `cond_wait`.** A thread can wake up spuriously, or another thread can take the job or slot before this one gets the mutex back.
- **Graceful drain.** After shutdown, consumers keep popping until the queue is empty, so no job that was already accepted is lost.
- **`signal` in push/pop, `broadcast` on shutdown.** One new job or one free slot lets only one thread proceed. Shutdown changes the situation for *every* waiting consumer at once.

---

## Part C: Print server (`C/print_server`)

### Core mode

```bash
./C/print_server [number-of-jobs]   # default 50
./C/print_server 50
```

1. Start **4 printer threads**, the consumers, **before** submitting anything. With a capacity of 8, main would otherwise block forever at job 9.
2. The main thread submits jobs `1..N` with `queue_push()`.
3. After the last job is accepted, call `queue_shutdown()`.
4. Join all printers. They drain the queue, then see that it is empty and shut down, and return.
5. Add up the per-printer `jobs_completed` counters and print the total.

Each printer updates only **its own** `jobs_completed`, so that counter needs no lock. `pthread_join()` guarantees main sees the final values.

Output format, which the autograder depends on:

```
SUBMITTED job=<id>
PROCESSED job=<id> printer=<printer-id>
TOTAL_COMPLETED <n>
```

Which printer handles which job, and how the lines interleave, is deliberately left unspecified. A `PROCESSED job=k` line can appear before its `SUBMITTED job=k` line, because main prints after `queue_push()` returns and a printer may take the job in between.

### Bonus mode: multiple concurrent producers

```bash
./C/print_server --bonus <producers> <jobs-per-producer>
./C/print_server --bonus 3 25
```

- One pthread per producer, all pushing into the **same** queue. There is no extra producer lock, because `queue_push()` is already safe to call from several threads.
- **Unique job IDs without shared state:** producer `p` uses the block `p·J + 1 … p·J + J`, where `J` is jobs per producer. Blocks never overlap, so the IDs need no lock.
- Each accepted job prints `SUBMITTED producer=<p> job=<id>`, with producer IDs `0..P-1`.
- **Required order:**
  1. Start the printers and the producers.
  2. **Join every producer.** Only then is it true that no more jobs will arrive.
  3. `queue_shutdown()`.
  4. Join every printer.
  5. Print `TOTAL_COMPLETED <P × J>`, then call `queue_destroy()`.

  Shutting down when only *one* producer has finished would make the other producers' pushes fail, and those jobs would be lost.
- Inputs where `P × J` would overflow the `int` job ID are rejected.

---

## Testing

| Test | Result |
|------|--------|
| Shell: handout transcript, bad `cd` usage, unknown commands, Ctrl-D | Matches the expected behaviour. The shell survives every error |
| Shell: zombie check | No zombie children left behind |
| Counter: `safe 8 100000` × 50 runs | 50/50 correct |
| Server: 10 scenarios × 30 runs (N = 1, 8, 9, 50, 1000; bonus 1×1, 3×25, 10×100, 50×40) | 300/300 passed: every job submitted once and processed exactly once, totals and per-producer counts correct, no hangs |
| ThreadSanitizer (`-fsanitize=thread`) | Unsafe counter flagged, as expected. **0 races** in safe mode, the server and the bonus |
| Valgrind memcheck | 0 errors, 0 bytes leaked |
| Valgrind Helgrind | Only false positives inside glibc's `printf` (stdout uses an internal lock Helgrind can't see) |

To reproduce the race-detector check (from the project folder):

```bash
gcc -std=c11 -O1 -g -D_POSIX_C_SOURCE=200809L -pthread -fsanitize=thread \
    C/print_server.c C/job_queue.c -o ps_tsan && ./ps_tsan --bonus 8 50
```

---

## Reasoning questions

Answers to all core questions and BONUS-Q are in [`answers.txt`](PA-3/PA3-shell-concurrency/answers.txt):

- **A-Q1–A-Q3:** why `execvp` runs in the child, why `cd` must be a built-in, zombies
- **B-Q1–B-Q3:** load/modify/store, a lost-update interleaving, why a race can still look correct
- **C-Q1–C-Q6:** one mutex for the whole queue, atomic unlock-and-sleep, `while` vs `if`, `broadcast` on shutdown, CVs vs busy-waiting, per-worker counters
- **BONUS-Q:** why shutdown waits until every producer has been joined
