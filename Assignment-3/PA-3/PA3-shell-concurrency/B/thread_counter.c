#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 100000L
#define MAX_THREADS 64

static long total_jobs = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    long iterations;
    int use_mutex;
} worker_arg_t;

static void *counter_worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;

    for (long i = 0; i < w->iterations; i++) {
        if (w->use_mutex) {
            /* TODO C1: lock counter_mutex. */
        }

        /* TODO B1: increment total_jobs exactly once per iteration.
         * In unsafe mode this increment is intentionally unsynchronized.
         */

        if (w->use_mutex) {
            /* TODO C2: unlock counter_mutex. */
        }
    }

    return NULL;
}

static long parse_long(const char *s, const char *name)
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

int main(int argc, char *argv[])
{
    const char *mode = (argc >= 2) ? argv[1] : "unsafe";
    long nthreads = (argc >= 3) ? parse_long(argv[2], "thread count") : DEFAULT_THREADS;
    long iterations = (argc >= 4) ? parse_long(argv[3], "iteration count") : DEFAULT_ITERATIONS;

    if (strcmp(mode, "unsafe") != 0 && strcmp(mode, "safe") != 0) {
        fprintf(stderr, "Usage: %s [unsafe|safe] [threads] [iterations]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (nthreads > MAX_THREADS) {
        fprintf(stderr, "Thread count must be <= %d\n", MAX_THREADS);
        return EXIT_FAILURE;
    }

    int use_mutex = (strcmp(mode, "safe") == 0);
    pthread_t threads[MAX_THREADS];
    worker_arg_t args[MAX_THREADS];

    total_jobs = 0;
    (void)counter_worker;
    (void)counter_mutex;

    /* TODO B2:
     * Initialize args[i] and create nthreads workers using pthread_create().
     * On failure, print an error and exit.
     */
    (void)threads;
    (void)args;

    /* TODO B3:
     * Join all created workers using pthread_join().
     */

    long expected = nthreads * iterations;
    printf("Mode: %s\n", mode);
    printf("Expected jobs: %ld\n", expected);
    printf("Actual jobs:   %ld\n", total_jobs);

    if (use_mutex && total_jobs != expected) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
