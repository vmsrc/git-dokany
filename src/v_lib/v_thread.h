#ifndef _V_THREAD_H_
#define _V_THREAD_H_

#if defined(_WIN32)
#include "v_thread.win32.h"
#elif defined(__unix__)
#include "v_thread.posix.h"
#else
#error Unsupported platform
#endif

// TODO : join with v_sem, v_csect ....
// maybe also autojoin threads

// semaphore
void v_sem_init(v_sem_t *sem, int count);
void v_sem_destroy(v_sem_t *sem);
void v_sem_post(v_sem_t *sem);
void v_sem_wait(v_sem_t *sem);
int v_sem_trywait(v_sem_t *sem);
int v_sem_timedwait(v_sem_t *sem, unsigned ms);

// a recursive critical section
void v_csect_init(v_csect_t *cs);
void v_csect_enter(v_csect_t *cs);
int v_csect_tryenter(v_csect_t *cs);
void v_csect_leave(v_csect_t *cs);
void v_csect_destroy(v_csect_t *cs);

// condition variable
void v_cond_init(v_cond_t *cv);
void v_cond_destroy(v_cond_t *cv);
void v_cond_wait(v_cond_t *restrict cv, v_csect_t *restrict cs);
int v_cond_timedwait(v_cond_t *restrict cv, v_csect_t *restrict cs, unsigned ms);
void v_cond_signal(v_cond_t *cv);
void v_cond_broadcast(v_cond_t *cv);

struct v_thread {
	v_thread_join_handle_t join;
	void *arg;
	void (*start_routine)(void *);
};

// v_thread_create initializes thr->join
void v_thread_create(struct v_thread *restrict thr, void (*start_routine)(void *), void *restrict arg);
// join is thr->join, initialized by v_thread_create above
void v_thread_join(v_thread_join_handle_t join);

void v_sched_yield(void);

#endif // _V_THREAD_H_
