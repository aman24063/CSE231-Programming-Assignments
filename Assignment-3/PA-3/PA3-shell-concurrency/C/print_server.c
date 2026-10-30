#include "job_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_PRINTERS 4
#define DEFAULT_JOBS 50

typedef struct {
    int id;
    job_queue_t *queue;
    long jobs_completed;
} worker_arg_t;

static void simulate_printing(void)
{
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L}; /* 1 ms */
    (void)nanosleep(&ts, NULL);
}

static void *printer_worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    int job = 0;

    while (queue_pop(w->queue, &job)) {
        simulate_printing();
        w->jobs_completed++;
        printf("PROCESSED job=%d printer=%d\n", job, w->id);
        fflush(stdout);
    }

    return NULL;
}

static long parse_positive_long(const char *s, const char *name)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value <= 0) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return value;
}

static int run_core_server(long num_jobs)
{
    job_queue_t queue;
    pthread_t printers[NUM_PRINTERS];
    worker_arg_t workers[NUM_PRINTERS] = {0};

    if (queue_init(&queue) != 0) {
        fprintf(stderr, "queue_init failed\n");
        return EXIT_FAILURE;
    }

    (void)printer_worker;

    /* TODO C13:
     * Initialize workers[i] and create NUM_PRINTERS printer_worker threads.
     */
    (void)printers;
    (void)workers;

    for (long job = 1; job <= num_jobs; job++) {
        if (queue_push(&queue, (int)job) != 0) {
            fprintf(stderr, "Failed to submit job %ld\n", job);
            queue_shutdown(&queue);
            queue_destroy(&queue);
            return EXIT_FAILURE;
        }
        printf("SUBMITTED job=%ld\n", job);
    }

    /* TODO C14: request graceful shutdown after the last submission. */

    /* TODO C15: join all printer threads. */

    long total = 0;
    for (int i = 0; i < NUM_PRINTERS; i++) {
        total += workers[i].jobs_completed;
    }

    printf("TOTAL_COMPLETED %ld\n", total);

    queue_destroy(&queue);
    return (total == num_jobs) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * BONUS (+10): Multiple concurrent client/producers.
 *
 * Required command line when implemented:
 *     ./print_server --bonus <producers> <jobs-per-producer>
 *
 * Each producer must run as its own pthread and submit exactly the requested
 * number of jobs into the SAME bounded queue while the printer threads consume
 * concurrently. Use unique positive job IDs. For every accepted job print:
 *
 *     SUBMITTED producer=<p> job=<id>
 *
 * The printer output remains:
 *
 *     PROCESSED job=<id> printer=<id>
 *
 * Main must join all producer threads BEFORE calling queue_shutdown(). Then it
 * must join the printer threads and print:
 *
 *     TOTAL_COMPLETED <n>
 *
 * The autograder awards bonus credit only when every submitted job is processed
 * exactly once and all producer counts are correct.
 */
static int run_bonus_server(long producers, long jobs_per_producer)
{
    (void)producers;
    (void)jobs_per_producer;
    fprintf(stderr, "BONUS_NOT_IMPLEMENTED\n");
    return 2;
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--bonus") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s --bonus <producers> <jobs-per-producer>\n", argv[0]);
            return EXIT_FAILURE;
        }
        long producers = parse_positive_long(argv[2], "producer count");
        long jobs = parse_positive_long(argv[3], "jobs per producer");
        return run_bonus_server(producers, jobs);
    }

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [number-of-jobs]\n", argv[0]);
        return EXIT_FAILURE;
    }

    long num_jobs = (argc == 2) ? parse_positive_long(argv[1], "job count") : DEFAULT_JOBS;
    return run_core_server(num_jobs);
}
