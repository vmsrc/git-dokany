#ifndef _V_ATOMIC_MSC_H_
#define _V_ATOMIC_MSC_H_

typedef __declspec(align(4)) int v_aligned_int_t;

static int v_atomic_add(v_aligned_int_t volatile *i, int v)
{
	return InterlockedAdd(i, v);
}

static int v_atomic_compare_exchange(v_aligned_int_t volatile *i, int cmp, int v)
{
	return InterlockedCompareExchange(i, v, cmp);
}

#endif // _V_ATOMIC_MSC_H_
