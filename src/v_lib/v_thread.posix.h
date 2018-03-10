#ifndef _V_THREAD_WIN32_H_
#define _V_THREAD_WIN32_H_

#include <pthread.h>
#include <semaphore.h>

typedef sem_t v_sem_t;

// should be a recursive mutex!
//extern pthread_mutexattr_t *v_csect_mutexattr;
//extern pthread_condattr_t *v_csect_condattr;

typedef pthread_mutex_t v_csect_t;
typedef pthread_cond_t v_cond_t;

extern pthread_attr_t *v_thread_attr;
typedef pthread_t v_thread_join_handle_t;

#endif // _V_THREAD_WIN32_H_
