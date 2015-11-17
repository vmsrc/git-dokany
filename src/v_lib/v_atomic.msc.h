#ifndef _V_ATOMIC_MSC_H_
#define _V_ATOMIC_MSC_H_

typedef __declspec(align(4)) int v_aligned_int_t;

static v_aligned_int_t v_atomic_add(v_aligned_int_t volatile *i, int v)
{
	return InterlockedAdd(i, v);
}

#endif // _V_ATOMIC_MSC_H_
