#include <stdlib.h>
#include <string.h>

#include "v_lib.h"
#include "v_abq.h"

void v_abq_init(struct v_abq *restrict abq, unsigned capacity, unsigned elem_sz, void *restrict storage)
{
	v_sem_init(&abq->used, 0);
	v_sem_init(&abq->free, capacity);
	v_csect_init(&abq->push_cs);
	v_csect_init(&abq->pop_cs);
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
	v_sem_destroy(&abq->used);
	v_sem_destroy(&abq->free);
	v_csect_destroy(&abq->push_cs);
	v_csect_destroy(&abq->pop_cs);
	if (abq->alloc)
		free(abq->storage);
}

void v_abq_push(struct v_abq *restrict abq, const void *restrict data)
{
	v_sem_wait(&abq->free);
	v_csect_enter(&abq->push_cs);
	char *push=abq->push;
	abq->push+=abq->elem_sz;
	if (abq->push==abq->last)
		abq->push=abq->storage;
	v_csect_leave(&abq->push_cs);
	memcpy(push, data, abq->elem_sz);
	v_sem_post(&abq->used);
}

void v_abq_pop(struct v_abq *restrict abq, void *restrict data)
{
	v_sem_wait(&abq->used);
	v_csect_enter(&abq->pop_cs);
	const char *pop=abq->pop;
	abq->pop+=abq->elem_sz;
	if (abq->pop==abq->last)
		abq->pop=abq->storage;
	v_csect_leave(&abq->pop_cs);
	memcpy(data, pop, abq->elem_sz);
	v_sem_post(&abq->free);
}
