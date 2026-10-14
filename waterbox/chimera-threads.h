/*
 * UAE's threads, run one at a time (the core's own; replaces
 * retrodep/threaddep/thread.h when CHIMERA_CORE is defined).
 *
 * The sandbox has one thread, and a run must come out the same every time.
 * UAE starts threads for the CD32's and the CDTV's drives and for CD audio,
 * and they wait on the computer's clock. Here each of them is a coroutine
 * with a stack of its own: it runs until it waits (a sleep, a semaphore, a
 * read of an empty pipe) and then hands back to the emulation, which wakes
 * it again at a line of the picture (chimera_threads_hsync) once the
 * emulated time it asked to sleep has passed, or at once when the emulation
 * itself waits for it.
 */

#ifndef CHIMERA_THREADS_H
#define CHIMERA_THREADS_H

typedef struct {
	void *sem;  /* not 0 once the semaphore is made (UAE looks at it) */
	int count;
} uae_sem_t;

typedef void *uae_thread_id;
#define BAD_THREAD 0

void uae_sem_init(uae_sem_t *sem, int pshared, unsigned int value);
void uae_sem_destroy(uae_sem_t *sem);
void uae_sem_post(uae_sem_t *sem);
void uae_sem_unpost(uae_sem_t *sem);
void uae_sem_wait(uae_sem_t *sem);
int uae_sem_trywait_delay(uae_sem_t *sem, int millis);
int uae_sem_trywait(uae_sem_t *sem);
int uae_sem_getvalue(uae_sem_t *sem, int *sval);

int uae_start_thread(const char *name, void (*f)(void *), void *arg, uae_thread_id *tid);
int uae_start_thread_fast(void (*f)(void *), void *arg, uae_thread_id *tid);
int uae_wait_thread(uae_thread_id tid);
void uae_kill_thread(uae_thread_id *tid);
uae_thread_id uae_thread_get_id(void);
void uae_end_thread(uae_thread_id *tid);
void uae_set_thread_priority(uae_thread_id *tid, int pri);

/* A sleep of the caller: a thread waits the emulated time, the emulation
   lets every thread run once. */
void chimera_threads_sleep(int ms);
/* Called once a line of the picture: the emulated clock moves on 64 us and
   the threads whose time has come run. */
void chimera_threads_hsync(void);

#include "commpipe.h"

#endif
