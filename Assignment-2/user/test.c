#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main() {
 int ticks = getuptime(); printf("System has been running for %d ticks\n", ticks);
 int activeprocs = activecount();
 printf("Number of active processes = %d \n", activeprocs);
 int mypid = getpid();
 printf("My PID is : %d\n", mypid);
 int count = lineage(mypid);
 printf("Count of Ancestors: %d\n", count);
 int numchildren = 5;
 for(int i = 0; i < numchildren; i++) {
 int pid = fork();
 if(pid < 0) {
 printf("test: fork failed\n");
 exit(1);
 }
 if(pid == 0) {
 for (volatile int i = 0; i < 10000000; i++);
 exit(0);
 }
 }
 for (volatile int i = 0; i < 10000; i++);
 int firstcount = familyheadcount(mypid);
 printf("Active children before exit: %d\n", firstcount);
 for (int i = 0; i < numchildren; i++) {
 wait(0);
 }
 int secondcount = familyheadcount(mypid);
 printf("Active children after exit: %d\n", secondcount);
 int memsize = getprocsize(mypid);
 printf("Memory size of process %d: %d bytes\n", mypid, memsize);
 exit(0);
}
