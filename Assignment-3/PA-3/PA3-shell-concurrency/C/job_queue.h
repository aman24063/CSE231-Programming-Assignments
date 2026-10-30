#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <pthread.h>

#define QUEUE_CAPACITY 8

typedef struct {
    int jobs[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int shutdown;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} job_queue_t;

/* Initialize/destroy the queue and its synchronization objects. */
int queue_init(job_queue_t *q);
void queue_destroy(job_queue_t *q);

/*
 * queue_push:
 *   Returns 0 when job is inserted.
 *   Returns -1 if shutdown has already been requested.
 */
int queue_push(job_queue_t *q, int job);

/*
 * queue_pop:
 *   Stores one job in *job and returns 1.
 *   Returns 0 only when shutdown is set AND the queue is empty.
 */
int queue_pop(job_queue_t *q, int *job);

/* No more jobs will be submitted. Wake any sleeping consumers. */
void queue_shutdown(job_queue_t *q);

#endif
