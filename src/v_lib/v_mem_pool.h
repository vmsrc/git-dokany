#ifndef _V_MEM_POOL_H_
#define _V_MEM_POOL_H_

struct v_mempool_block {
	struct v_mempool_block *next;
	char data[];
};

struct v_mempool {
	size_t esize; // element size
	size_t dsize; // data size in the block
	size_t bytes; // number of free bytes in the current block
	void *free;   // list of elements, freed using v_mempool_free(pool, ptr)
	struct v_mempool_block *first, *current;
};

void v_mempool_init(struct v_mempool *pool, size_t esize, size_t elements);
void *v_mempool_alloc(struct v_mempool *pool);

/* frees one element for reuse on next alloc.
 * does nothing when esize<sizeof(void *)
 */
void v_mempool_freeone(struct v_mempool *restrict pool, void *restrict ptr);
void v_mempool_clear(struct v_mempool *pool);
void v_mempool_destroy(struct v_mempool *pool);

#endif // _V_MEM_POOL_H_
