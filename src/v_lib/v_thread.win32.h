#ifndef _V_THREAD_WIN32_H_
#define _V_THREAD_WIN32_H_

#define WIN32_LEAN_AND_MEAN
#include "windows.h"

// semaphore HANDLE
typedef void *v_sem_t;

typedef CRITICAL_SECTION v_csect_t;
typedef CONDITION_VARIABLE v_cond_t;

// thread HANDLE
typedef void *v_thread_join_handle_t;

#endif // _V_THREAD_WIN32_H_
