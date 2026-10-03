#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/procinfo.h"
#include "user/user.h"

struct procinfo procs[NPROC];
int n, showpid, showmem;

void show(int i, int depth){
  for(int d = 1; d < depth; d++) printf("   ");
  if(depth > 0) printf("|- ");
  printf("%s", procs[i].name);
  if(showpid) printf("(%d)", procs[i].pid);
  if(showmem) printf(" [%dB]", (int)procs[i].sz);
  printf("\n");
  for(int j = 0; j < n; j++)
    if(j != i && procs[j].ppid == procs[i].pid)
      show(j, depth + 1);
}

int main(int argc, char *argv[]){
  for(int i = 1; i < argc; i++)
    if(argv[i][0] == '-')
      for(char *c = argv[i] + 1; *c; c++){
        if(*c == 'p') showpid = 1;
        if(*c == 'm') showmem = 1;
      }
  n = getprocinfo(procs, NPROC);
  for(int i = 0; i < n; i++)
    if(procs[i].pid == 1) show(i, 0);
  exit(0);
}
