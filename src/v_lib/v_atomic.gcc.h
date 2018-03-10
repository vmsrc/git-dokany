#ifndef _V_ATOMIC_GCC_H_
#define _V_ATOMIC_GCC_H_

typedef int v_aligned_int_t __attribute__ ((aligned (4)));

// TODO inline
static __attribute__ ((unused)) v_aligned_int_t v_atomic_add(v_aligned_int_t volatile *i, int v)
{
	return __sync_fetch_and_add(i, v);
}

#endif // _V_ATOMIC_GCC_H_
