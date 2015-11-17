#ifndef _V_ATOMIC_H_
#define _V_ATOMIC_H_

// typedef'd v_aligned_int_t

#if defined(_MSC_VER)
#include "v_atomic.msc.h"
#elif defined(__GNUC__)
#include "v_atomic.gcc.h"
#else
#error Unsupported platform
#endif

// TODO compareexchange, etc
int v_atomic_add(v_aligned_int_t volatile *i, int v);

#endif // _V_ATOMIC_H_
