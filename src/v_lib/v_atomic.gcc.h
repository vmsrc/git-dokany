#ifndef _V_ATOMIC_GCC_H_
#define _V_ATOMIC_GCC_H_

typedef int v_aligned_int_t __attribute__ ((aligned (4)));

// TODO inline
static __attribute__ ((unused)) int v_atomic_add(v_aligned_int_t volatile *i, int v)
{
	return __sync_fetch_and_add(i, v);
}

static __attribute__ ((unused)) int v_atomic_compare_exchange(v_aligned_int_t volatile *i, int cmp, int v)
{
	return __sync_val_compare_and_swap(i, cmp, v);
}

#endif // _V_ATOMIC_GCC_H_
