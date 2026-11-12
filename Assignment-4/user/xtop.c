// Bonus: xtop -- a live, top-style process monitor.
//   xtop            redraw every 10 ticks until killed
//   xtop -d N       redraw every N ticks
//   xtop -n F       stop after F frames
//
// User space cannot read the kernel's proc[] table, so everything here
// comes from system calls: getpids() for the list of live pids, then
// schedinfo() for each one, plus uptime() and schedpolicy(-1).
// %CPU = (rticks now - rticks at previous frame) * 100 / interval.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/schedinfo.h"
#include "user/user.h"

struct row {
  struct schedinfo si;
  int pct; // %CPU over the last interval
};

// previous frame's rticks, looked up by pid
static int prevpid[NPROC], prevrt[NPROC], nprev;

static struct row rows[NPROC];
static int nrows;

// ---- tiny output buffer, so each frame is a single write() ----
static char out[8192];
static int outn;

static void
puts_(const char *s)
{
  while (*s && outn < sizeof(out) - 1)
    out[outn++] = *s++;
}

static void
putnum(int v)
{
  char tmp[16];
  int i = 0, neg = v < 0;
  unsigned int u = neg ? -v : v;
  do {
    tmp[i++] = '0' + u % 10;
    u /= 10;
  } while (u);
  if (neg)
    tmp[i++] = '-';
  while (i > 0 && outn < sizeof(out) - 1)
    out[outn++] = tmp[--i];
}

static void
puthex(int v)
{
  char tmp[16];
  int i = 0;
  unsigned int u = v;
  do {
    tmp[i++] = "0123456789abcdef"[u % 16];
    u /= 16;
  } while (u);
  puts_("0x");
  while (i > 0 && outn < sizeof(out) - 1)
    out[outn++] = tmp[--i];
}

// pad with spaces so the column that started at 'start' is 'w' wide
static void
padto(int start, int w)
{
  do {
    puts_(" ");
  } while (outn - start < w);
}

static char *
statename(int s)
{
  switch (s) {
  case PS_USED:     return "used";
  case PS_SLEEPING: return "sleep";
  case PS_RUNNABLE: return "runble";
  case PS_RUNNING:  return "run";
  case PS_ZOMBIE:   return "zombie";
  default:          return "?";
  }
}

static int
lookup_prev(int pid, int rticks)
{
  for (int i = 0; i < nprev; i++)
    if (prevpid[i] == pid)
      return prevrt[i];
  return rticks; // new since last frame: no history, count 0
}

// Take one snapshot of every process into rows[].
static void
sample(void)
{
  int pids[NPROC];
  int n = getpids(pids, NPROC);

  nrows = 0;
  for (int i = 0; i < n; i++) {
    // The process may have exited (and been reaped) between getpids()
    // and schedinfo(); that is not an error, just skip it.
    if (schedinfo(pids[i], &rows[nrows].si) < 0)
      continue;
    nrows++;
  }
}

static void
remember(void)
{
  nprev = nrows;
  for (int i = 0; i < nrows; i++) {
    prevpid[i] = rows[i].si.pid;
    prevrt[i] = rows[i].si.rticks;
  }
}

static void
draw(int now, int interval)
{
  int me = getpid();
  int runnable = 0, harts = 0, nh = 0;
  int busy[NCPU];

  for (int h = 0; h < NCPU; h++)
    busy[h] = 0;

  for (int i = 0; i < nrows; i++) {
    struct schedinfo *si = &rows[i].si;
    int d = si->rticks - lookup_prev(si->pid, si->rticks);
    rows[i].pct = d * 100 / interval;
    if (si->pid != me && (si->state == PS_RUNNING || si->state == PS_RUNNABLE))
      runnable++;
    harts |= si->affinity | (1 << si->lastcpu);
    if (si->lastcpu >= 0 && si->lastcpu < NCPU)
      busy[si->lastcpu] += d; // approximate: credit ticks to last hart
  }
  for (int h = 0; h < NCPU; h++)
    if (harts & (1 << h))
      nh = h + 1;

  // sort by %CPU (descending), ties by pid -- insertion sort, n <= 64
  for (int i = 1; i < nrows; i++) {
    struct row r = rows[i];
    int j = i - 1;
    while (j >= 0 && (rows[j].pct < r.pct ||
                      (rows[j].pct == r.pct && rows[j].si.pid > r.si.pid))) {
      rows[j + 1] = rows[j];
      j--;
    }
    rows[j + 1] = r;
  }

  outn = 0;
  puts_("\x1b[H\x1b[2J"); // cursor home + clear screen
  puts_("xtop up ");
  putnum(now);
  puts_(" ticks procs ");
  putnum(nrows);
  puts_(" runnable ");
  putnum(runnable);
  puts_(" policy ");
  puts_(schedpolicy(-1) == POLICY_PRIO ? "PRIO" : "RR");
  puts_("\n");
  for (int h = 0; h < nh; h++) {
    int pct = busy[h] * 100 / interval;
    if (pct > 100)
      pct = 100; // per-hart timers drift slightly from hart 0's ticks
    puts_("hart");
    putnum(h);
    puts_(" ");
    putnum(pct);
    puts_("%  ");
  }
  puts_("\n\nPID  NAME        STATE   PRIO  CPU  MASK   SLICE  %CPU\n");
  for (int i = 0; i < nrows; i++) {
    struct schedinfo *si = &rows[i].si;
    int s = outn;
    putnum(si->pid);     padto(s, 5);  s = outn;
    puts_(si->name);     padto(s, 12); s = outn;
    puts_(statename(si->state)); padto(s, 8); s = outn;
    putnum(si->prio);    padto(s, 6);  s = outn;
    putnum(si->lastcpu); padto(s, 5);  s = outn;
    puthex(si->affinity); padto(s, 7); s = outn;
    putnum(si->slice);   padto(s, 7);
    putnum(rows[i].pct);
    puts_("\n");
  }
  write(1, out, outn);
}

static void
usage(void)
{
  fprintf(2, "usage: xtop [-d ticks] [-n frames]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  int delay = 10, frames = -1; // -1 = forever

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      delay = atoi(argv[++i]);
      if (delay < 1)
        usage();
    } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      frames = atoi(argv[++i]);
      if (frames < 1)
        usage();
    } else {
      usage();
    }
  }

  // Baseline sample, so even the first frame has a real %CPU.
  int t0 = uptime();
  sample();
  remember();

  for (int f = 0; frames < 0 || f < frames; f++) {
    pause(delay);
    int t1 = uptime();
    int interval = t1 - t0;
    if (interval < 1)
      interval = 1;
    sample();
    draw(t1, interval);
    remember();
    t0 = t1;
  }
  exit(0);
}
