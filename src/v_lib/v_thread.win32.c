#include <limits.h>

#define WIN32_LEAN_AND_MEAN
#include "windows.h"

#include "v_lib.h"
#include "v_thread.h"

void v_sem_init(v_sem_t *sem, int count) {
	*sem=CreateSemaphore(NULL, count, INT_MAX, NULL);
}

void v_sem_destroy(v_sem_t *sem) {
	CloseHandle(*sem);
}

void v_sem_post(v_sem_t *sem) {
	ReleaseSemaphore(*sem, 1, NULL);
}

void v_sem_wait(v_sem_t *sem) {
	WaitForSingleObject(*sem, INFINITE);
}

int v_sem_trywait(v_sem_t *sem) {
	return WaitForSingleObject(*sem, 0)!=WAIT_OBJECT_0;
}

int v_sem_timedwait(v_sem_t *sem, unsigned ms) {
	if (ms>=INFINITE)
		--ms;
	return WaitForSingleObject(*sem, ms)!=WAIT_OBJECT_0;
}

void v_csect_init(v_csect_t *cs) {
	InitializeCriticalSection(cs);
}

void v_csect_enter(v_csect_t *cs) {
	EnterCriticalSection(cs);
}

int v_csect_tryenter(v_csect_t *cs) {
	return TryEnterCriticalSection(cs)==0;
}

void v_csect_leave(v_csect_t *cs) {
	LeaveCriticalSection(cs);
}

void v_csect_destroy(v_csect_t *cs) {
	DeleteCriticalSection(cs);
}

void v_cond_init(v_cond_t *cv) {
	InitializeConditionVariable(cv);
}

void v_cond_destroy(v_cond_t *cv) {
}

void v_cond_wait(v_cond_t *restrict cv, v_csect_t *restrict cs) {
	SleepConditionVariableCS(cv, cs, INFINITE);
}

int v_cond_timedwait(v_cond_t *restrict cv, v_csect_t *restrict cs, unsigned ms) {
	if (ms>=INFINITE)
		--ms;
	return SleepConditionVariableCS(cv, cs, ms)!=WAIT_OBJECT_0;
}

void v_cond_signal(v_cond_t *cv) {
	WakeConditionVariable(cv);
}

void v_cond_broadcast(v_cond_t *cv) {
	WakeAllConditionVariable(cv);
}

static DWORD WINAPI threadProc(LPVOID lpParameter) {
	struct v_thread *thr=lpParameter;
	thr->start_routine(thr->arg);
	return 0;
}	

void v_thread_create(struct v_thread *restrict thr, void (*start_routine)(void *), void *restrict arg) {
	thr->arg=arg;
	thr->start_routine=start_routine;
	thr->join=CreateThread(NULL, 0, threadProc, thr, 0, NULL);
}

void v_thread_join(v_thread_join_handle_t join) {
	WaitForSingleObject(join, INFINITE);
	CloseHandle(join);
}

void v_sched_yield(void) {
	Sleep(0);
}
