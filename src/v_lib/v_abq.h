#ifndef _V_ABQ_H_
#define _V_ABQ_H_

#include "v_thread.h"

struct v_abq {
	v_sem_t free, used;
	v_csect_t push_cs, pop_cs;
	char *storage, *push, *pop, *last;
	int alloc;
	unsigned elem_sz;
};

void v_abq_init(struct v_abq *restrict abq, unsigned capacity, unsigned elem_sz, void *restrict storage);
void v_abq_destroy(struct v_abq *abq);
void v_abq_push(struct v_abq *restrict abq, const void *restrict data);
void v_abq_pop(struct v_abq *restrict abq, void *restrict data);

#endif // _V_ABQ_H_
