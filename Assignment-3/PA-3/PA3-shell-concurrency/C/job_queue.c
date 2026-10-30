#include "job_queue.h"

#include <stddef.h>

int queue_init(job_queue_t *q)
{
    if (q == NULL) {
        return -1;
    }

    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = 0;

    /* TODO C3:
     * Initialize q->mutex, q->not_empty, and q->not_full.
     * If an initialization fails, clean up anything already initialized
     * and return -1. Return 0 on success.
     */

    return 0;
}

void queue_destroy(job_queue_t *q)
{
    if (q == NULL) {
        return;
    }

    /* TODO C4: destroy the mutex and both condition variables. */
}

int queue_push(job_queue_t *q, int job)
{
    if (q == NULL) {
        return -1;
    }

    /* TODO C5-C7:
     * 1. Lock q->mutex.
     * 2. While the queue is full and shutdown is false, wait on not_full.
     * 3. If shutdown is set, unlock and return -1.
     * 4. Insert job at tail, advance tail circularly, increment count.
     * 5. Signal a consumer through not_empty.
     * 6. Unlock and return 0.
     *
     * IMPORTANT: pthread_cond_wait() must be guarded by a while loop.
     */

    (void)job;
    return -1;
}

int queue_pop(job_queue_t *q, int *job)
{
    if (q == NULL || job == NULL) {
        return 0;
    }

    /* TODO C8-C10:
     * 1. Lock q->mutex.
     * 2. While the queue is empty and shutdown is false, wait on not_empty.
     * 3. If queue is empty AND shutdown is true, unlock and return 0.
     * 4. Remove the job at head, advance head circularly, decrement count.
     * 5. Signal a producer through not_full.
     * 6. Unlock and return 1.
     */

    return 0;
}

void queue_shutdown(job_queue_t *q)
{
    if (q == NULL) {
        return;
    }

    /* TODO C11-C12:
     * 1. Lock q->mutex.
     * 2. Set q->shutdown = 1.
     * 3. Wake ALL consumers sleeping on not_empty.
     * 4. Unlock q->mutex.
     *
     * Explain in answers.txt why broadcast(), rather than signal(),
     * is appropriate during shutdown.
     */
}
