CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O0 -g -D_POSIX_C_SOURCE=200809L
PTHREAD := -pthread

.PHONY: all clean check

all: A/msh B/thread_counter C/print_server

A/msh: A/minishell.c
	$(CC) $(CFLAGS) $< -o $@

B/thread_counter: B/thread_counter.c
	$(CC) $(CFLAGS) $(PTHREAD) $< -o $@

C/print_server: C/print_server.c C/job_queue.c C/job_queue.h
	$(CC) $(CFLAGS) $(PTHREAD) C/print_server.c C/job_queue.c -o $@

check: all
	python3 grader.py

clean:
	rm -f A/msh B/thread_counter C/print_server
