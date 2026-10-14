/*
 * UAE's threads, run one at a time: see chimera-threads.h.
 *
 * Each thread has a stack of its own and runs until it waits; then it hands
 * back to the emulation, which is the only thing that starts a thread
 * running. Which thread runs when depends on nothing but the emulation, so
 * a run comes out the same every time, natively and in the sandbox, and a
 * savestate (all of the machine's memory) holds the threads where they are.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "threaddep/thread.h"

/* A reference build for the tests keeps upstream's real threads instead */
#ifndef CHIMERA_REAL_THREADS

#define MAX_THREADS 64
#define STACK_SIZE (1024 * 1024)

enum { T_READY, T_SLEEP, T_SEMWAIT, T_DONE };

struct cthread {
	void *sp;           /* where its registers were left */
	void *stack;
	void (*f)(void *);
	void *arg;
	int state;
	int64_t wake_us;    /* T_SLEEP: the emulated time it waits for */
	uae_sem_t *sem;     /* T_SEMWAIT: the semaphore it waits on */
	int named;          /* its id was handed out: the slot is never reused */
};

static struct cthread *threads[MAX_THREADS];
static struct cthread *current;  /* 0 while the emulation runs */
static void *emulation_sp;
static int64_t now_us;           /* the emulated clock, 64 us a line */
static int emulation_id;         /* what uae_thread_get_id gives the emulation */

/* Switch stacks: keep the registers a function must keep (and the floating
   point control words) on this stack, leave the stack pointer in *from, and
   go on from *to. x86-64, System V; the same in both builds. */
void chimera_co_swap(void **from, void **to);
__asm__(
	".text\n"
	".globl chimera_co_swap\n"
	".type chimera_co_swap,@function\n"
	"chimera_co_swap:\n"
	"	pushq %rbp\n"
	"	pushq %rbx\n"
	"	pushq %r12\n"
	"	pushq %r13\n"
	"	pushq %r14\n"
	"	pushq %r15\n"
	"	subq $8, %rsp\n"
	"	stmxcsr (%rsp)\n"
	"	fnstcw 4(%rsp)\n"
	"	movq %rsp, (%rdi)\n"
	"	movq (%rsi), %rsp\n"
	"	ldmxcsr (%rsp)\n"
	"	fldcw 4(%rsp)\n"
	"	addq $8, %rsp\n"
	"	popq %r15\n"
	"	popq %r14\n"
	"	popq %r13\n"
	"	popq %r12\n"
	"	popq %rbx\n"
	"	popq %rbp\n"
	"	ret\n"
	".size chimera_co_swap, .-chimera_co_swap\n");

static void yield_to_emulation(void)
{
	struct cthread *t = current;
	chimera_co_swap(&t->sp, &emulation_sp);
}

static void thread_entry(void)
{
	struct cthread *t = current;
	t->f(t->arg);
	t->state = T_DONE;
	yield_to_emulation();
	abort();  /* a finished thread is never run again */
}

static void run(struct cthread *t)
{
	current = t;
	chimera_co_swap(&emulation_sp, &t->sp);
	current = NULL;
	if (t->state == T_DONE && t->stack) {
		munmap(t->stack, STACK_SIZE);
		t->stack = NULL;
	}
}

static int may_run(const struct cthread *t, int force)
{
	switch (t->state) {
	case T_READY: return 1;
	case T_SLEEP: return force || t->wake_us <= now_us;
	case T_SEMWAIT: return t->sem->count > 0;
	default: return 0;
	}
}

/* Run once each thread that may run, in the order they were started. With
   force, a sleeping thread runs too: the emulation is waiting for it. */
static int run_round(int force)
{
	int ran = 0;
	for (int i = 0; i < MAX_THREADS; i++) {
		struct cthread *t = threads[i];
		if (!t || !may_run(t, force))
			continue;
		if (t->state == T_SEMWAIT)
			t->sem->count--;
		t->state = T_READY;
		run(t);
		ran = 1;
	}
	return ran;
}

void chimera_threads_hsync(void)
{
	now_us += 64;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (threads[i] && may_run(threads[i], 0)) {
			run_round(0);
			return;
		}
	}
}

void chimera_threads_sleep(int ms)
{
	if (current) {
		current->state = T_SLEEP;
		current->wake_us = now_us + (int64_t)(ms < 0 ? -ms : ms) * 1000;
		yield_to_emulation();
	} else {
		run_round(1);
	}
}

void uae_sem_init(uae_sem_t *sem, int pshared, unsigned int value)
{
	(void)pshared;
	if (!sem || sem->sem)
		return;
	sem->sem = sem;
	sem->count = (int)value;
}

void uae_sem_destroy(uae_sem_t *sem)
{
	if (sem)
		sem->sem = NULL;
}

void uae_sem_post(uae_sem_t *sem)
{
	if (sem && sem->sem)
		sem->count++;
}

void uae_sem_unpost(uae_sem_t *sem)
{
	if (sem && sem->sem)
		sem->count = 0;
}

void uae_sem_wait(uae_sem_t *sem)
{
	if (!sem || !sem->sem)
		return;
	if (current) {
		if (sem->count > 0) {
			sem->count--;
			return;
		}
		current->state = T_SEMWAIT;
		current->sem = sem;
		yield_to_emulation();  /* the count was taken for it */
		return;
	}
	while (sem->count <= 0) {
		if (!run_round(1)) {
			write_log("chimera: the emulation waits on a semaphore no thread will post\n");
			return;
		}
	}
	sem->count--;
}

int uae_sem_trywait_delay(uae_sem_t *sem, int millis)
{
	(void)millis;
	if (!sem || !sem->sem)
		return -1;
	if (sem->count > 0) {
		sem->count--;
		return 0;
	}
	return -1;
}

int uae_sem_trywait(uae_sem_t *sem)
{
	return uae_sem_trywait_delay(sem, 0);
}

int uae_sem_getvalue(uae_sem_t *sem, int *sval)
{
	if (!sem || !sem->sem)
		return -1;
	*sval = sem->count;
	return 0;
}

int uae_start_thread(const char *name, void (*f)(void *), void *arg, uae_thread_id *tid)
{
	(void)name;
	int slot = -1;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (!threads[i] || (threads[i]->state == T_DONE && !threads[i]->named)) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		write_log("chimera: no room for another thread\n");
		return 0;
	}
	struct cthread *t = threads[slot];
	if (!t) {
		t = calloc(1, sizeof *t);
		if (!t)
			return 0;
	}
	void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
	                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (stack == MAP_FAILED) {
		if (!threads[slot])
			free(t);
		return 0;
	}
	memset(t, 0, sizeof *t);
	t->stack = stack;
	t->f = f;
	t->arg = arg;
	t->state = T_READY;

	/* What chimera_co_swap pops: the control words, six registers, and the
	   return into thread_entry, which then finds the stack as a call left
	   it. */
	uint64_t *top = (uint64_t *)((char *)stack + STACK_SIZE);
	*--top = 0;
	*--top = (uint64_t)(uintptr_t)thread_entry;
	for (int i = 0; i < 6; i++)
		*--top = 0;
	*--top = 0x1f80 | ((uint64_t)0x037f << 32);
	t->sp = top;

	threads[slot] = t;
	if (tid) {
		*tid = t;
		t->named = 1;
	}
	return 1;
}

int uae_start_thread_fast(void (*f)(void *), void *arg, uae_thread_id *tid)
{
	return uae_start_thread(NULL, f, arg, tid);
}

int uae_wait_thread(uae_thread_id tid)
{
	struct cthread *t = tid;
	while (t && t->state != T_DONE) {
		if (current) {
			chimera_threads_sleep(1);
		} else if (!run_round(1)) {
			write_log("chimera: the emulation waits for a thread that cannot run\n");
			return -1;
		}
	}
	return 0;
}

void uae_kill_thread(uae_thread_id *tid)
{
	(void)tid;
}

uae_thread_id uae_thread_get_id(void)
{
	return current ? (void *)current : (void *)&emulation_id;
}

void uae_end_thread(uae_thread_id *tid)
{
	if (tid)
		*tid = NULL;
}

void uae_set_thread_priority(uae_thread_id *tid, int pri)
{
	(void)tid;
	(void)pri;
}

#endif /* CHIMERA_REAL_THREADS */
