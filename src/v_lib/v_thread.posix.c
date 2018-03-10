#include <pthread.h>
#include <semaphore.h>

#include "v_lib.h"
#include "v_thread.h"

void v_sem_init(v_sem_t *sem, int count) {
	sem_init(sem, 0, count);
}

void v_sem_destroy(v_sem_t *sem) {
	sem_destroy(sem);
}

void v_sem_post(v_sem_t *sem) {
	sem_post(sem);
}

void v_sem_wait(v_sem_t *sem) {
	sem_wait(sem);
}

int v_sem_trywait(v_sem_t *sem) {
	return sem_trywait(sem);
}

int v_sem_timedwait(v_sem_t *sem, unsigned ms);

void v_csect_init(v_csect_t *cs) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(cs, &attr);
	pthread_mutexattr_destroy(&attr);
}

void v_csect_enter(v_csect_t *cs) {
	pthread_mutex_lock(cs);
}

int v_csect_tryenter(v_csect_t *cs) {
	return pthread_mutex_trylock(cs);
}

void v_csect_leave(v_csect_t *cs) {
	pthread_mutex_unlock(cs);
}

void v_csect_destroy(v_csect_t *cs) {
	pthread_mutex_destroy(cs);
}

void v_cond_init(v_cond_t *cv) {
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_cond_init(cv, &attr);
	pthread_condattr_destroy(&attr);
}

void v_cond_destroy(v_cond_t *cv) {
	pthread_cond_destroy(cv);
}

void v_cond_wait(v_cond_t *restrict cv, v_csect_t *restrict cs) {
	pthread_cond_wait(cv, cs);
}

#if 0
int v_cond_timedwait(v_cond_t *restrict cv, v_csect_t *restrict cs, unsigned ms) {
}

void v_cond_signal(v_cond_t *cv) {
}
#endif
void v_cond_broadcast(v_cond_t *cv) {
	pthread_cond_broadcast(cv);
}

static void *threadProc(void *p) {
	struct v_thread *thr=p;
	thr->start_routine(thr->arg);
	return NULL;
}	

void v_thread_create(struct v_thread *restrict thr, void (*start_routine)(void *), void *restrict arg) {
	thr->arg=arg;
	thr->start_routine=start_routine;
	pthread_create(&thr->join, NULL, threadProc, thr);
}

void v_thread_join(v_thread_join_handle_t join) {
	void *res;
	pthread_join(join, &res);
}

void v_sched_yield(void) {
	sched_yield();
}
