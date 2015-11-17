#include <stdlib.h>

#include "v_lib.h"
#include "v_misc.h"
#include "v_mem_pool.h"

void v_mempool_init(struct v_mempool *pool, size_t esize, size_t elements)
{
	size_t dsize=esize*elements;
	if (dsize<esize || dsize<elements || !esize ||!elements || dsize + sizeof(struct v_mempool_block)<dsize)
		esize=dsize=0;
	pool->esize=esize;
	pool->dsize=dsize;
	pool->bytes=0;
	pool->free=NULL;
	pool->first=pool->current=NULL;
}

void *v_mempool_alloc(struct v_mempool *pool)
{
	if (pool->free) {
		void *res=pool->free;
		pool->free=*(void **)res;
		return res;
	}
	if (!pool->bytes) {
		if (pool->current && pool->current->next) {
			pool->current=pool->current->next;
		} else {
			if (!pool->esize)
				return NULL;
			struct v_mempool_block *new=malloc(pool->dsize + sizeof(struct v_mempool_block));
			if (!new)
				return NULL;
			new->next=NULL;
			if (pool->current)
				pool->current->next=new;
			else
				pool->first=new;
			pool->current=new;
		}
		pool->bytes=pool->dsize;
	}
	pool->bytes-=pool->esize;
	return pool->current->data + pool->bytes;
}

void v_mempool_freeone(struct v_mempool *restrict pool, void *restrict element)
{
	if (pool->esize>=sizeof(void *)) {
		*(void **)element=pool->free;
		pool->free=element;
	}
}

void v_mempool_clear(struct v_mempool *pool)
{
	pool->free=NULL;
	pool->current=pool->first;
	pool->bytes=pool->first ? pool->dsize : 0;
}

void v_mempool_destroy(struct v_mempool *pool)
{
	struct v_mempool_block *b=pool->first, *tmp;
	while (b) {
		tmp=b;
		b=b->next;
		free(tmp);
	}
	pool->first=pool->current=NULL;
	pool->bytes=0;
}
