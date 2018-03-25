#include <stdlib.h>
#include <string.h>

#include "v_lib.h"
#include "v_abq.h"

void v_abq_init(struct v_abq *restrict abq, unsigned capacity, unsigned elem_sz, void *restrict storage)
{
	v_sem_init(&abq->blockPush, 0);
	v_sem_init(&abq->blockPop, 0);
	abq->toPush=capacity;
	abq->toPop=0;
	abq->lockPush=0;
	abq->lockPop=0;
	abq->elem_sz=elem_sz;
	abq->alloc=0;
	if (!storage) {
		storage=malloc(elem_sz*capacity);
		abq->alloc=1;
	}
	abq->push=abq->pop=abq->storage=storage;
	abq->last=abq->storage + capacity*elem_sz;
}

void v_abq_destroy(struct v_abq *abq)
{
	v_sem_destroy(&abq->blockPop);
	v_sem_destroy(&abq->blockPush);
	if (abq->alloc)
		free(abq->storage);
}

void v_abq_push(struct v_abq *restrict abq, const void *restrict data)
{
	int n=v_atomic_add(&abq->toPush, -1);
	if (n<0)
		v_sem_wait(&abq->blockPush);

	while (v_atomic_compare_exchange(&abq->lockPush, 0, 1)!=0)
		;

	char *push=abq->push;
	abq->push+=abq->elem_sz;
	if (abq->push==abq->last)
		abq->push=abq->storage;

	v_atomic_compare_exchange(&abq->lockPush, 1, 0);

	memcpy(push, data, abq->elem_sz);
	n=v_atomic_add(&abq->toPop, 1);
	if (n<=0)
		v_sem_post(&abq->blockPop);
}

void v_abq_pop(struct v_abq *restrict abq, void *restrict data)
{
	int n=v_atomic_add(&abq->toPop, -1);
	if (n<0)
		v_sem_wait(&abq->blockPop);

	while (v_atomic_compare_exchange(&abq->lockPop, 0, 1)!=0)
		;

	const char *pop=abq->pop;
	abq->pop+=abq->elem_sz;
	if (abq->pop==abq->last)
		abq->pop=abq->storage;

	v_atomic_compare_exchange(&abq->lockPop, 1, 0);

	memcpy(data, pop, abq->elem_sz);
	n=v_atomic_add(&abq->toPush, 1);
	if (n<=0)
		v_sem_post(&abq->blockPush);
}
