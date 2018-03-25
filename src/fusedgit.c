#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define FUSE_STAT stat
#define FUSE_OFF_T off_t
#include <errno.h>
#endif

#define FUSE_USE_VERSION 27

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <assert.h>

#include "fuse.h"
#include "git2.h"
#include "sys.h"

#include "v_mem_pool.h"
#include "v_misc.h"
#include "v_thread.h"
#include "v_abq.h"
#include "v_atomic.h"
#include "fusedgit.h"

#if 0
#ifdef assert
#undef assert
#endif
#define assert(x) ((x) ? 0 : assertFail(__FILE__, __LINE__, #x))

static int assertFail(const char *file, int line, const char *what)
{
	printf("file: %s, line: %d, msg: %s", file, line, what);
	abort();
	return 0;
}
#endif

#define off_t FUSE_OFF_T

#define CHUNKS_IN_BLOCK 500
#define CHUNK_DATA_SIZE 4000

struct arena_block
{
	struct arena_block *next;
	char data[];
};

struct arena {
	size_t dsize; // data size in the block
	size_t bytes; // number of free bytes in the current block
	struct arena_block *first, *current;
};

void arena_init(struct arena *arena, size_t dsize)
{
	arena->dsize=dsize;
	arena->bytes=dsize;
	arena->first=arena->current=NULL;
}

void *arena_alloc(struct arena *arena, size_t size)
{
	assert(size<=arena->dsize);
	if (arena->bytes + size<=arena->dsize) {
		void *res=arena->current->data + arena->bytes;
		arena->bytes+=size;
		return res;
	}
	struct arena_block *block=malloc(arena->dsize + sizeof(struct arena_block));
	block->next=NULL;
	arena->bytes=size;
	if (arena->current) {
		arena->current->next=block;
		arena->current=block;
	} else {
		arena->current=arena->first=block;
	}
	return block->data;
}

void arena_destroy(struct arena *arena)
{
	int n=0;
	while (arena->first) {
		++n;
		struct arena_block *tmp=arena->first;
		arena->first=arena->first->next;
		free(tmp);
	}
}

struct fusedgit {
	struct fuse_chan *fuseChan;
	int fuseArgc;
	char **fuseArgv;
	struct fuse *fuse;
	char *fuseMountPt;

	struct arena arena;
	v_sem_t fuseThrSem;
	struct v_thread fuseThr;
	int fuseRes;
	int fuseDaemon;

	v_csect_t csFileChunks;
	struct v_mempool fileChunksPool;
	v_csect_t csOtherPools;
	struct v_mempool fsEntryPool, fsUniqEntryPool;
	struct fsEntry *rootEntry;
	struct fsUniqEntry *uniq;

	struct uniqFileEntry *cacheFirst, *cacheLast, *openFirst;

	u32 cacheChunks, openChunks;
	u32 maxChunks;

	int threads;
	v_csect_t csGlob;
	v_cond_t *qConds;
	struct v_abq abqCondi;

	fusedgit_repo_t firstRepo, lastRepo;
};

struct chunk {
	struct chunk *left, *right, *next;
	char data[CHUNK_DATA_SIZE];
};

#define SZVAL_UNKNOWN ((size_t)-1)
struct uniqFileEntry {
	// 0 - closed; 1 - cached; 2 - opened
	int state;
	v_aligned_int_t openCnt;
	u64 statSz;
	u64 loadSz;
	u32 numChunks;
	struct chunk *chunks;
	struct uniqFileEntry *prev, *next;
};

struct uniqTreeEntry {
	int childCnt; // <0 - not initialized
	struct fsEntry **childs;
};

struct fsUniqEntry {
	int condi;

	git_oid oid;

	union {
		struct uniqFileEntry file;
		struct uniqTreeEntry tree;
	};

	// for by oid uniq entries
	struct fsUniqEntry *left, *right;
	int refcount;
};

struct fsEntry {
	struct fusedgit *fusedgit;
	git_repository *repo; // ==NULL -> pure virtual entry
	git_odb *odb;

	int virtCnt; // <0 - file, >=0 - dir + zero or more virtual tree childs
	struct fsEntry **virtChilds;

	char *name;
	struct fsUniqEntry *uniq;
};

static int oid_cmp(const git_oid *o1, const git_oid *o2)
{
	u32 *pu1=(u32 *)o1->id;
	u32 *pu2=(u32 *)o2->id;
	for (int i=0; i<GIT_OID_RAWSZ/4; i++) {
		if (pu1[i]!=pu2[i])
			return pu1[i]<pu2[i] ? -1 : 1;
	}
	return 0;
}

static struct fsUniqEntry **searchUniqEntry(struct fsUniqEntry **puniq, const git_oid *restrict oid)
{
	struct fsUniqEntry *uniq=*puniq;
	struct fsUniqEntry **prev=puniq;
	int cmp=0;
	while (uniq) {
		cmp=oid_cmp(oid, &uniq->oid);
		if (!cmp)
			break;
		if (cmp>0) {
			prev=&uniq->right;
			uniq=uniq->right;
		} else {
			prev=&uniq->left;
			uniq=uniq->left;
		}
	}
	return prev;
}

#ifdef _WIN32
static uchar winToLower(uchar c)
{
	if (c>='A' && c<='Z')
		c+='a' - 'A';
	return c;
}
#define winStrCmp stricmp
#else
#define winToLower(x) x
#define winStrCmp strcmp
#endif

// returns:
// -1 - ss1 < ss2
//  0 - matching strings
//  1 - ss1 > ss2
// on - index of the first different char or strlen
static int mystrcmp(const char *ss1, const char *ss2, int *on)
{
	const uchar *us1=(const uchar *)ss1, *us2=(const uchar *)ss2;
	int n=0;
	int res=0;
	while (us1[n] && us2[n]) {
		uchar uc1=winToLower(us1[n]);
		uchar uc2=winToLower(us2[n]);
		if (uc1<uc2) {
			res=-1;
			goto exit;
		}
		if (uc1>uc2) {
			res=1;
			goto exit;
		}
		++n;
	}
	if (us2[n])
		res=-1;
	if (us1[n])
		res=1;
exit:
	if (on)
		*on=n;
	return res;
}

static int entryComparator(const void *v1, const void *v2)
{
	const struct fsEntry * const *pe1=v1, * const *pe2=v2;
	const struct fsEntry *e1=*pe1, *e2=*pe2;
	return winStrCmp(e1->name, e2->name);
}

static void allocUniq(struct fsEntry *fse, const git_oid *oid)
{
	struct fusedgit *fusedgit=fse->fusedgit;

	struct fsUniqEntry **puniq=NULL;
	struct fsUniqEntry *uniq=NULL;
	assert(fse->repo);// if (fse->repo) {
		// not a pure virtual directory - search uniq
		puniq=searchUniqEntry(&fusedgit->uniq, oid);
		uniq=*puniq;
		if (uniq)
			uniq->refcount++;
	//}
	if (!uniq) {
		uniq=v_mempool_alloc(&fusedgit->fsUniqEntryPool);
		uniq->oid=*oid;
		uniq->left=uniq->right=NULL;
		uniq->condi=-1;
		uniq->refcount=1;
		if (fse->virtCnt<0) {
			struct uniqFileEntry *file=&uniq->file;
			file->chunks=NULL;
			file->numChunks=0;
			file->next=file->prev=NULL;
			file->state=0;
			file->openCnt=0;
			file->statSz=file->loadSz=SZVAL_UNKNOWN;
		} else {
			struct uniqTreeEntry *tree=&uniq->tree;
			tree->childCnt=-1;
			tree->childs=NULL;
		}
		if (puniq)
			*puniq=uniq;
	}
	fse->uniq=uniq;
}

static struct fsEntry *allocFsEntry(
		const char *name,
		struct fusedgit *fusedgit,
		git_repository *repo,
		git_odb *odb,
		const git_oid *oid,
		int isFile
	)
{
	
	struct fsEntry *entry=v_mempool_alloc(&fusedgit->fsEntryPool);
	if (entry) {
		entry->name=arena_alloc(&fusedgit->arena, strlen(name) + 1);
		strcpy(entry->name, name);
		entry->uniq=NULL;
		entry->fusedgit=fusedgit;
		entry->repo=repo;
		entry->odb=odb;
		entry->virtCnt=isFile ? -1 : 0;
		entry->virtChilds=NULL;
		if (oid)
			allocUniq(entry, oid);
	}
	return entry;
}

static void loadStat(struct fsEntry *fse)
{
	struct fusedgit *fgit=fse->fusedgit;
	if (fse->virtCnt<0) {
		struct uniqFileEntry *file=&fse->uniq->file;
		if (file->statSz==SZVAL_UNKNOWN) {
			v_csect_enter(&fgit->csGlob);
			if (file->statSz==SZVAL_UNKNOWN) {
				file->statSz=0;
				size_t len;
				git_otype otype;
				if (!git_odb_read_header(&len, &otype, fse->odb, &fse->uniq->oid)) {
					barrier();
					file->statSz=len;
				}
			}
			v_csect_leave(&fgit->csGlob);
		}
	}
}

static void toFuseStat(struct fsEntry *fse, struct FUSE_STAT *stbuf, int zero)
{
	if (zero)
		memset(stbuf, 0, sizeof(*stbuf));

	if (fse->virtCnt<0) {
		loadStat(fse);
		stbuf->st_mode=S_IFREG | 0444;
		stbuf->st_nlink=1;
		stbuf->st_size=fse->uniq->file.statSz;
	} else {
		stbuf->st_mode=S_IFDIR | 0555;
		stbuf->st_nlink=2;
	}
}

static void loadTree(struct fsEntry *fse)
{
	struct fusedgit *fgit=fse->fusedgit;
	struct fsUniqEntry *uniq=fse->uniq;

	if (!uniq)
		return;

	struct uniqTreeEntry *tree=&uniq->tree;

	if (tree->childCnt>=0)
		return;

	int condi;
	v_abq_pop(&fgit->abqCondi, &condi);
	v_csect_enter(&fgit->csGlob);
	if (tree->childCnt>=0) {
		assert(uniq->condi<0);
		v_abq_push(&fgit->abqCondi, &condi);
		v_csect_leave(&fgit->csGlob);
		return;
	}
	if (uniq->condi>=0) {
		v_abq_push(&fgit->abqCondi, &condi);
		while (uniq->condi>=0)
			v_cond_wait(fgit->qConds + uniq->condi, &fgit->csGlob);
		assert(tree->childCnt>=0);
		v_csect_leave(&fgit->csGlob);
		return;
	}
	uniq->condi=condi;
	v_csect_leave(&fgit->csGlob);

	assert(tree->childCnt<0);
	int childCnt=0;
	git_tree *tObj;
	if (!git_tree_lookup(&tObj, fse->repo, &uniq->oid)) {
		
		const git_tree *treeObj=(const git_tree *)tObj;
		int max_i=(int)git_tree_entrycount(treeObj);
		const git_tree_entry *te;
		if (max_i>0 && max_i<1000000) {
			v_csect_enter(&fgit->csOtherPools);
			tree->childs=arena_alloc(&fgit->arena, sizeof(struct fsEntry *)*max_i);
			v_csect_leave(&fgit->csOtherPools);
			if (!tree->childs)
				abort();
			for (int i=0; i < max_i; ++i) {
				te=git_tree_entry_byindex(treeObj, i);
				git_otype otype=git_tree_entry_type(te);
				if (otype==GIT_OBJ_TREE || otype==GIT_OBJ_BLOB) {
					const git_oid *oid=git_tree_entry_id(te);
					const char *name=git_tree_entry_name(te);
					for (int j=0; j<fse->virtCnt; j++) {
						if (!winStrCmp(fse->virtChilds[j]->name, name)) {
							fse->virtChilds[j]->name[0]='\0';
							printf("warning - directory overriden: %s\n", name);
						}
					}
					v_csect_enter(&fgit->csOtherPools);
					tree->childs[childCnt++]=allocFsEntry(name,
						fgit, fse->repo, fse->odb, oid, (otype==GIT_OBJ_BLOB));
					v_csect_leave(&fgit->csOtherPools);
				}
			}
			qsort(tree->childs, childCnt, sizeof(tree->childs[0]), entryComparator);
		}
		git_tree_free(tObj);
	}
	assert(tree->childCnt<0);

	v_csect_enter(&fgit->csGlob);
	assert(uniq->condi>=0);
	v_cond_broadcast(fgit->qConds + uniq->condi);
	v_abq_push(&fgit->abqCondi, &uniq->condi);
	uniq->condi=-1;
	barrier();
	tree->childCnt=childCnt;
	v_csect_leave(&fgit->csGlob);
}

// return NULL - resolved exactly
//       !=NULL - points to the unresolved part in path, e.g. saerching "/1/2/3/path/to/searchfile" in "/1/2/3/path" will return "to/searchfile"
static const char *resolveDirEntry(struct fsEntry **entry_out, struct fsEntry *fseIn, const char *path)
{
	struct fsUniqEntry *uniq=fseIn->uniq;
	if (uniq) {
		if (uniq->tree.childCnt<0)
			loadTree(fseIn);
		struct uniqTreeEntry *treeIn=&uniq->tree;

		int min=0;
		int max=treeIn->childCnt - 1;
		while (min<=max) {
			int mid=(max - min)/2 + min;
			struct fsEntry *child=treeIn->childs[mid];
			int n=0;
			int r=(child->name && child->name[0]) ? mystrcmp(child->name, path, &n) : -1;
			if (r<0 && child->virtCnt>=0 && path[n]=='/' && !child->name[n])
				return resolveDirEntry(entry_out, child, path + n + 1);
			if (path[n]=='/')
				r=1;
			if (r<0) {
				min=mid + 1;
			} else if (r>0) {
				max=mid - 1;
			} else {
				*entry_out=child;
				return NULL;
			}
		}
	}
	for (int i=0; i<fseIn->virtCnt; i++) {
		struct fsEntry *child=fseIn->virtChilds[i];
		int n=0;
		int r=mystrcmp(child->name, path, &n);
		if (r<0 && child->virtCnt>=0 && path[n]=='/' && !child->name[n])
			return resolveDirEntry(entry_out, child, path + n + 1);
		if (!r) {
			*entry_out=child;
			return NULL;
		}
	}
	*entry_out=fseIn;
	return path;
}

static const char *resolveRootEntry(struct fsEntry **entry_out, struct fsEntry *root, const char *path)
{
	if (path[0])
		return resolveDirEntry(entry_out, root, path);
	// the root directory itself
	*entry_out=root;
	return NULL;
}

static int fuseGetattr(const char *path, struct FUSE_STAT *stbuf)
{
	int res=-ENOENT;
	memset(stbuf, 0, sizeof(*stbuf));
	++path; // skip first '/'
	fusedgit_t fgit=fuse_get_context()->private_data;
	struct fsEntry *entry;
	const char *resolv=resolveRootEntry(&entry, fgit->rootEntry, path);
	if (!resolv) {
		toFuseStat(entry, stbuf, 0);
		res=0;
	}
	return res;
}

static int fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	int res=-ENOENT;
	++path; // skip first '/'
	fusedgit_t fgit=fuse_get_context()->private_data;
	struct fsEntry *entry;
	const char *resolv=resolveRootEntry(&entry, fgit->rootEntry, path);
	if (!resolv) {
		if (entry->virtCnt>=0) {

			struct FUSE_STAT fuseStat;
			toFuseStat(entry, &fuseStat, 1);
			res=filler(buf, ".", &fuseStat, 0);
			if (!res)
				res=filler(buf, "..", &fuseStat, 0);
			loadTree(entry);
			if (entry->uniq) {
				struct uniqTreeEntry *tree=&entry->uniq->tree;
				for (int i=0; !res && i<tree->childCnt; i++) {
					struct fsEntry *child=tree->childs[i];
					toFuseStat(child, &fuseStat, 1);
					res=filler(buf, child->name, &fuseStat, 0);
				}
			}
			toFuseStat(entry, &fuseStat, 1);
			for (int i=0; !res && i<entry->virtCnt; i++) {
				struct fsEntry *child=entry->virtChilds[i];
				assert(child->virtCnt>=0);
				if (child->name[0])
					res=filler(buf, child->name, &fuseStat, 0);
			}
		}
	}
	return res;
}

static int fuseOpen(const char *path, struct fuse_file_info *fi)
{
	int res=-ENOENT;
	++path; // skip first '/'
	fusedgit_t fgit=fuse_get_context()->private_data;
	struct fsEntry *entry;
	const char *resolv=resolveRootEntry(&entry, fgit->rootEntry, path);
	if (!resolv && entry->virtCnt<0) {
		if ((fi->flags & 3) != O_RDONLY) {
			res=-EACCES;
		} else {
			res=0;
			fi->fh=(size_t)entry;
			v_atomic_add(&entry->uniq->file.openCnt, 1);
		}
	}
	return res;
}

#if 0
static void prlist(struct fusedgit *fgit)
{
	printf(">Cached: ");
	struct uniqFileEntry *f=fgit->cacheFirst;
	int lastfound=0;
	int n=1;
	while (f) {
		printf("%d ", f->numChunks);
		if (f && f==fgit->cacheLast);
			lastfound=n;
		++n;
		f=f->next;
	}
	printf("last: %d ", lastfound);
	int firstfound=0;
	f=fgit->cacheLast;
	n=1;
	while (f) {
		printf("%d ", f->numChunks);
		if (f && f==fgit->cacheFirst);
			firstfound=n;
		++n;
		f=f->prev;
	}
	printf("first: %d ", firstfound);

	f=fgit->openFirst;
	printf(">Open: ");
	while (f) {
		printf("%d ", f->numChunks);
		f=f->next;
	}
	printf("\n");
}
#endif

static void reduceCache(struct fusedgit *fgit)
{
	v_csect_enter(&fgit->csGlob);
	while (fgit->cacheChunks && fgit->cacheChunks + fgit->openChunks > fgit->maxChunks) {
		struct uniqFileEntry *cached=fgit->cacheLast;
		assert(cached);
		assert(cached->state==1);
		fgit->cacheLast=cached->prev;
		if (!fgit->cacheLast) {
			fgit->cacheFirst=NULL;
			assert(fgit->cacheChunks==cached->numChunks);
		} else {
			fgit->cacheLast->next=NULL;
		}
		cached->prev=cached->next=NULL;
		assert(fgit->cacheChunks>=cached->numChunks);
		fgit->cacheChunks-=cached->numChunks;
		struct chunk *chunk=cached->chunks;
		cached->state=0;
		cached->chunks=NULL;
		cached->loadSz=SZVAL_UNKNOWN;
		cached->numChunks=0;

		v_csect_enter(&fgit->csFileChunks);
		while (chunk) {
			struct chunk *c=chunk;
			chunk=chunk->next;
			v_mempool_freeone(&fgit->fileChunksPool, c);
		}
		v_csect_leave(&fgit->csFileChunks);

		v_csect_leave(&fgit->csGlob);
		v_sched_yield();
		v_csect_enter(&fgit->csGlob);
	}
	v_csect_leave(&fgit->csGlob);
}

static int fuseRelease(const char *path, struct fuse_file_info *fi)
{
	int res=-ENOENT;
	++path; // skip first '/'
	fusedgit_t fgit=fuse_get_context()->private_data;
	struct fsEntry *entry;
	const char *resolv=resolveRootEntry(&entry, fgit->rootEntry, path);
	if (!resolv && entry->virtCnt<0) {
		res=0;
		struct uniqFileEntry *file=&entry->uniq->file;
		int closed=0;
		v_csect_enter(&fgit->csGlob);
		int count=v_atomic_add(&file->openCnt, -1);
		if (count==0 && file->state==2) {
			struct uniqFileEntry *next=file->next, *prev=file->prev;
			if (next)
				next->prev=prev;
			if (prev)
				prev->next=next;
			if (fgit->openFirst==file)
				fgit->openFirst=next;

			fgit->openChunks-=file->numChunks;
			fgit->cacheChunks+=file->numChunks;

			file->prev=NULL;
			file->next=fgit->cacheFirst;
			if (fgit->cacheFirst)
				fgit->cacheFirst->prev=file;
			fgit->cacheFirst=file;
			if (!fgit->cacheLast)
				fgit->cacheLast=file;
			file->state=1;
			closed=1;
		}
		v_csect_leave(&fgit->csGlob);
		// moved to cache -> reduce
		if (closed)
			reduceCache(fgit);
	}
	return res;
}

struct myGitStrem {
	git_odb_stream *stream;
	git_odb_object *obj;
	const void *odata;
	u64 osize;
};

static void myGitStreamOpen(struct myGitStrem *stream, git_odb *odb, git_oid *oid)
{
	stream->obj=NULL;
	int res=git_odb_open_rstream(&stream->stream, odb, oid);
	if (res) {
		stream->stream=NULL;
		res=git_odb_read(&stream->obj, odb, oid);
		if (res || !stream->obj) {
			stream->obj=NULL;
		} else {
			stream->odata=git_odb_object_data(stream->obj);
			stream->osize=git_odb_object_size(stream->obj);
			if (!stream->odata)
				stream->osize=0;
			if (!stream->osize)
				stream->odata=NULL;
		}
	}
}

static void myGitStreamClose(struct myGitStrem *stream)
{
#define freeTMP free
#undef free
	if (stream->stream)
		stream->stream->free(stream->stream);
#define free freeTMP
	if (stream->obj)
		git_odb_object_free(stream->obj);
}

static size_t myGitStreamRead(struct myGitStrem *stream, void *buf, size_t len)
{
	size_t res=0;
	if (stream->stream) {
		int rd=stream->stream->read(stream->stream, buf, len);
		res=rd>0 ? rd : 0;
	} else if (stream->obj && stream->odata && stream->osize) {
		if (len > stream->osize)
			len=stream->osize;
		memcpy(buf, stream->odata, len);
		stream->odata=(const char *)stream->odata + len;
		stream->osize-=len;
		res+=len;
	}
	return res;
}

static void loadFileChunks(struct fsEntry *fileEntry)
{
	struct fusedgit *fgit=fileEntry->fusedgit;
	struct uniqFileEntry *file=&fileEntry->uniq->file;

	reduceCache(fgit);

	size_t statLen=file->statSz;
	u32 numChunks=(u32)((statLen + (CHUNK_DATA_SIZE - 1))/CHUNK_DATA_SIZE);
	file->numChunks=numChunks;
	file->loadSz=file->statSz;
	if (!file->loadSz)
		return;

	u32 level=1, leveli=0;
	u32 i;
	struct chunk *back=NULL, *uplevel=NULL, *levelStart=NULL;
	size_t loadLen=0;
	size_t len=CHUNK_DATA_SIZE;

	v_csect_enter(&fgit->csFileChunks);
	struct chunk *chunk=v_mempool_alloc(&fgit->fileChunksPool);
	file->chunks=chunk;
	for (i=1; i<numChunks; i++) {
		struct chunk *next=v_mempool_alloc(&fgit->fileChunksPool);
		chunk->next=next;
		chunk=next;
	}
	chunk->next=NULL;
	v_csect_leave(&fgit->csFileChunks);

	chunk=file->chunks;
	struct myGitStrem stream;
	myGitStreamOpen(&stream, fileEntry->odb, &fileEntry->uniq->oid);

	for (i=0; i<numChunks; i++) {
		if (!levelStart)
			levelStart=chunk;
		chunk->left=chunk->right=NULL;
		if (back)
			back->next=chunk;
		back=chunk;
		if (uplevel) {
			if (leveli&1) {
				uplevel->right=chunk;
				uplevel=uplevel->next;
			} else {
				uplevel->left=chunk;
			}
		}
		if (++leveli>=level) {
			leveli=0;
			level<<=1;
			uplevel=levelStart;
			if (!file->chunks)
				file->chunks=levelStart;
			levelStart=NULL;
		}
		if (len>statLen)
			len=statLen;
		size_t nrd=myGitStreamRead(&stream, chunk->data, len);
		loadLen+=nrd;
		if (nrd!=len)
			break;
		statLen-=len;
		chunk=chunk->next;
	}
	file->loadSz=loadLen;
	myGitStreamClose(&stream);

	reduceCache(fgit);
}

static void loadFile(struct fsEntry *fileEntry)
{
	struct fusedgit *fgit=fileEntry->fusedgit;
	struct fsUniqEntry *uniq=fileEntry->uniq;
	struct uniqFileEntry *file=&uniq->file;

	if (file->state==2)
		return;

	assert(file->openCnt>0);
	int condi;
	v_abq_pop(&fgit->abqCondi, &condi);
	v_csect_enter(&fgit->csGlob);
	if (file->state==2) {
		assert(uniq->condi<0);
		v_abq_push(&fgit->abqCondi, &condi);
		v_csect_leave(&fgit->csGlob);
		return;
	}
	if (file->state==1) {
		assert(uniq->condi<0);
		v_abq_push(&fgit->abqCondi, &condi);
		struct uniqFileEntry *prev=file->prev, *next=file->next;
		if (next)
			next->prev=prev;
		if (prev)
			prev->next=next;

		// remove from cached list
		if (fgit->cacheFirst==file)
			fgit->cacheFirst=next;
		if (fgit->cacheLast==file)
			fgit->cacheLast=prev;
		assert(fgit->cacheChunks>=file->numChunks);
		fgit->cacheChunks-=file->numChunks;
		// add to opened list
		fgit->openChunks+=file->numChunks;

		file->prev=NULL;
		file->next=fgit->openFirst;
		if (fgit->openFirst)
			fgit->openFirst->prev=file;
		fgit->openFirst=file;
		barrier();
		file->state=2;
		v_csect_leave(&fgit->csGlob);
		return;
	}
	if (uniq->condi>=0) {
		v_abq_push(&fgit->abqCondi, &condi);
		while (uniq->condi>=0)
			v_cond_wait(fgit->qConds + uniq->condi, &fgit->csGlob);
		assert(file->state==2);
		v_csect_leave(&fgit->csGlob);
		return;
	}
	uniq->condi=condi;
	v_csect_leave(&fgit->csGlob);

	assert(!file->state);
	assert(!file->chunks);
	loadFileChunks(fileEntry);
	assert(!file->state);

	v_csect_enter(&fgit->csGlob);
	assert(uniq->condi>=0);
	v_cond_broadcast(fgit->qConds + uniq->condi);
	v_abq_push(&fgit->abqCondi, &uniq->condi);
	uniq->condi=-1;
	// add to opened list
	fgit->openChunks+=file->numChunks;
	file->prev=NULL;
	file->next=fgit->openFirst;
	if (fgit->openFirst)
		fgit->openFirst->prev=file;
	fgit->openFirst=file;
	barrier();
	file->state=2;
	v_csect_leave(&fgit->csGlob);
}

#if 0
static u32 msbitMask(u32 val)
{
	u32 mask=0x80000000;
	while (mask && !(mask&val))
		mask>>=1;
	return mask;
}
#endif


#if defined(_MSC_VER)
static u32 msbitMask(u32 b)
{
	assert(b);
	u32 ix;
	_BitScanReverse(&ix, b);
	return 1<<ix;
}
#elif defined(__GNUC__)
static u32 msbitMask(u32 b)
{
	u32 ix=__builtin_clz(b);
	return 0x80000000>>ix;
}
#else
static u32 msbitMask(u32 v)
{
	if(v&0xffff0000)if(v&0xff000000)if(v&0xf0000000)if(v&0xc0000000)if(v&0x80000000)
		return 0x80000000;else return 0x40000000;
	else if(v&0x20000000)return 0x20000000;else return 0x10000000;
	else if(v&0xc000000)if(v&0x8000000)return 0x8000000;else return 0x4000000;
	else if(v&0x2000000)return 0x2000000;else return 0x1000000;
	else if(v&0xf00000)if(v&0xc00000)if(v&0x800000)return 0x800000;else return 0x400000;
	else if(v&0x200000)return 0x200000;else return 0x100000;
	else if(v&0xc0000)if(v&0x80000)return 0x80000;else return 0x40000;
	else if(v&0x20000)return 0x20000;else return 0x10000;
	else if(v&0xff00)if(v&0xf000)if(v&0xc000)if(v&0x8000)return 0x8000;else return 0x4000;
	else if(v&0x2000)return 0x2000;else return 0x1000;
	else if(v&0xc00)if(v&0x800)return 0x800;else return 0x400;
	else if(v&0x200)return 0x200;else return 0x100;
	else if(v&0xf0)if(v&0xc0)if(v&0x80)return 0x80;else return 0x40;
	else if(v&0x20)return 0x20;else return 0x10;
	else if(v&0xc)if(v&0x8)return 0x8;else return 0x4;
	else if(v&0x2)return 0x2;else return v;
}
#endif

static int readFile(struct fsEntry *fileEntry, size_t offset, size_t size, char *buf)
{
	struct uniqFileEntry *file=&fileEntry->uniq->file;
	int res=0;
	if (size>INT_MAX)
		size=INT_MAX;
	if (file->statSz > offset) {
		loadFile(fileEntry);
		struct chunk *chunk=file->chunks;
		if (offset>=file->loadSz)
			size=0;
		if (offset + size > file->loadSz) {
			size=file->loadSz - offset;
		}
		res=(u32)size;
		chunk=file->chunks;
		u32 div=(u32)(offset/CHUNK_DATA_SIZE) + 1;
		u32 mod=offset%CHUNK_DATA_SIZE;
		u32 mask=msbitMask(div)>>1;
		while (mask) {
			chunk=(div&mask) ? chunk->right : chunk->left;
			mask>>=1;
		}

		size_t s=size;
		if (s>CHUNK_DATA_SIZE - mod)
			s=CHUNK_DATA_SIZE - mod;
		memcpy(buf, chunk->data + mod, s);
		chunk=chunk->next;
		buf+=s;
		size-=s;
		size_t whole=size/CHUNK_DATA_SIZE;
		int i;
		for (i=0; i<whole; i++) {
			memcpy(buf, chunk->data, CHUNK_DATA_SIZE);
			chunk=chunk->next;
			buf+=CHUNK_DATA_SIZE;
		}
		size%=CHUNK_DATA_SIZE;
		if (size)
			memcpy(buf, chunk->data, size);
	}
	return res;
}

static int fuseRead(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	int res=-ENOENT;
	++path; // skip first '/'
	fusedgit_t fgit=fuse_get_context()->private_data;
	const char *resolv=NULL;
	struct fsEntry *entry=(struct fsEntry *)(size_t)fi->fh;
	if (!entry)
		resolv=resolveRootEntry(&entry, fgit->rootEntry, path);
	if (!resolv && entry->virtCnt<0) {
		res=0;
		if ((fi->flags & 3) != O_RDONLY)
			res=-EACCES;
	}
	if (!res && offset>=0) {
		res=readFile(entry, offset, size, buf);
	}
	return res;
}

static void *fuseInit(struct fuse_conn_info *conn)
{
	return fuse_get_context()->private_data;
}

static void fuseLoopThread(void *param)
{
	fusedgit_t fgit=param;
	static const struct fuse_operations fuseOperations = {
		.getattr = fuseGetattr,
		.readdir = fuseReaddir,
		.open    = fuseOpen,
		.read    = fuseRead,
		.release = fuseRelease,
		.init    = fuseInit,
	};
	// 	return fuse_main(argc, argv, &fuseOperations, fusedgit);
	//fusedgit->fuse=fuse_setup(1, &mountpoint, &fuseOperations, sizeof(fuseOperations), &fusedgit->mountpoint, &fusedgit->multithreaded, fusedgit);
	struct fuse_args args = {
		.argc=fgit->fuseArgc,
		.argv=fgit->fuseArgv,
		.allocated=0
	};
	int multithreaded;
	int foreground;
	int res;

	struct fuse *fuse=NULL;
	fgit->fuseChan=NULL;
	fgit->fuseMountPt=NULL;
	res=fuse_parse_cmdline(&args, &fgit->fuseMountPt, &multithreaded, &foreground);
	if (!res) {
		fgit->fuseChan=fuse_mount(fgit->fuseMountPt, &args);
		res=(fgit->fuseChan==NULL);
	}
	if (!res) {
		fuse=fuse_new(fgit->fuseChan, &args, &fuseOperations, sizeof(fuseOperations), fgit);
		res=(fuse==NULL);
	}
	fgit->fuseDaemon=!foreground;
	if (!res)
		res=fuse_daemonize(foreground);
	//fuse_set_signal_handlers(fuse_get_session(fuse));
	if (!res) {
		fgit->fuseRes=0;
		if (!fgit->fuseDaemon)
			v_sem_post(&fgit->fuseThrSem);
		// ??? MT loops are only supported on MSVC - blah blah
		fgit->fuse=fuse;
		if (multithreaded)
			fgit->fuseRes=fuse_loop_mt(fuse);
		else
			fgit->fuseRes=fuse_loop(fuse);
	} else {
		fgit->fuseRes=1;
	}
	v_sem_post(&fgit->fuseThrSem);
}

int fusedgit_mount(fusedgit_t fgit, int argc, char *argv[])
{
	fgit->fuseDaemon=0;
	fgit->fuseArgc=argc;
	fgit->fuseArgv=argv;
	v_thread_create(&fgit->fuseThr, fuseLoopThread, fgit);
	v_sem_wait(&fgit->fuseThrSem);
	return fgit->fuseRes;
}

int fusedgit_umount(fusedgit_t fgit)
{
	int res=1;
	if (fgit->fuseChan) {
		fuse_unmount(fgit->fuseMountPt, fgit->fuseChan);
		v_thread_join(fgit->fuseThr.join);
		res=fgit->fuseRes;
		fgit->fuseChan=NULL;
		fgit->fuse=NULL;
	}
	return res;
}

fusedgit_t fusedgit_create(int threads, unsigned long long cacheSize)
{
	if (threads<1)
		threads=4;
	fusedgit_t fgit=malloc(sizeof(*fgit));
	arena_init(&fgit->arena, 2*1024*1024);
	v_sem_init(&fgit->fuseThrSem, 0);
	fgit->fuseChan=NULL;
	fgit->fuseArgc=0;
	fgit->fuseArgv=NULL;
	fgit->fuse=NULL;
	fgit->fuseDaemon=0;
	fgit->rootEntry=NULL;
	fgit->uniq=NULL;
	v_mempool_init(&fgit->fileChunksPool, sizeof(struct chunk), CHUNKS_IN_BLOCK);
	v_mempool_init(&fgit->fsEntryPool, sizeof(struct fsEntry), 1000000/sizeof(struct fsEntry));
	v_mempool_init(&fgit->fsUniqEntryPool, sizeof(struct fsUniqEntry), 1000000/sizeof(struct fsUniqEntry));
	fgit->cacheFirst=fgit->cacheLast=fgit->openFirst=NULL;
	fgit->cacheChunks=fgit->openChunks=0;
	u64 maxChunks=cacheSize/sizeof(struct chunk);
	if (maxChunks>UINT_MAX)
		maxChunks=UINT_MAX;
	fgit->maxChunks=(u32)maxChunks;
	v_csect_init(&fgit->csGlob);
	v_csect_init(&fgit->csFileChunks);
	v_csect_init(&fgit->csOtherPools);
	fgit->threads=threads;
	fgit->qConds=malloc(sizeof(v_cond_t)*threads);
	v_abq_init(&fgit->abqCondi, threads, sizeof(int), NULL);
	fgit->firstRepo=fgit->lastRepo=NULL;
	for (int i=0; i<threads; i++) {
		v_cond_init(fgit->qConds + i);
		v_abq_push(&fgit->abqCondi, &i);
	}
	git_libgit2_init();
	//git_libgit2_opts(GIT_OPT_SET_CACHE_MAX_SIZE, (size_t)(1024*1024));
	git_libgit2_opts(GIT_OPT_ENABLE_CACHING, 0);
	git_libgit2_opts(GIT_OPT_SET_MWINDOW_SIZE, (size_t)(1024*1024));
	git_libgit2_opts(GIT_OPT_SET_MWINDOW_MAPPED_LIMIT, (size_t)(32*1024*1024));
	return fgit;
}

#if 0
static void freeFSEntries(struct fsEntry *fse)
{
	// assert(!fse->uniq->hasCond)
	if (fse) {
		if (fse->uniq && --fse->uniq->refcount==0) {
			for (int i=0; i<fse->uniq->tree.childCnt; i++)
				freeFSEntries(fse->uniq->tree.childs[i]);
			if (fse->virtCnt>=0)
				free(fse->uniq->tree.childs);
		}
		for (int i=0; i<fse->virtCnt; i++)
			freeFSEntries(fse->virtChilds[i]);
		free(fse->virtChilds);
		free(fse->name);
	}
}
#endif

struct fusedgit_tmp_repo {
	fusedgit_t fusedgit;
	git_repository *repo;
	char *repoPath;
	git_odb *odb;
	int entries;
	struct fusedgit_tmp_repo *next;
};

void fusedgit_destroy(fusedgit_t fgit)
{
	if (!fgit)
		return;
	v_sem_destroy(&fgit->fuseThrSem);
	//freeFSEntries(fgit->rootEntry);
	arena_destroy(&fgit->arena);
	for (int i=0; i<fgit->threads; i++) {
		int condi;
		v_abq_pop(&fgit->abqCondi, &condi);
		v_cond_destroy(fgit->qConds + i);
	}
	free(fgit->qConds);
	v_abq_destroy(&fgit->abqCondi);
	v_csect_destroy(&fgit->csGlob);
	v_csect_destroy(&fgit->csFileChunks);
	v_csect_destroy(&fgit->csOtherPools);
	v_mempool_destroy(&fgit->fsEntryPool);
	v_mempool_destroy(&fgit->fsUniqEntryPool);
	v_mempool_destroy(&fgit->fileChunksPool);
	fusedgit_repo_t repo=fgit->firstRepo;
	while (repo) {
		git_odb_free(repo->odb);
		git_repository_free(repo->repo);
		free(repo->repoPath);
		fusedgit_repo_t tmp=repo;
		repo=repo->next;
		free(tmp);
	}
	free(fgit);
	git_libgit2_shutdown();
}

fusedgit_repo_t fusedgit_addrepo(fusedgit_t fusedgit, const char *repoPath)
{
	fusedgit_repo_t tmprepo=NULL;
	git_repository *repo;
	if (!git_repository_open_ext(&repo, repoPath, GIT_REPOSITORY_OPEN_NO_SEARCH, NULL)) {
		git_odb *odb;
		if (!git_repository_odb(&odb, repo)) {
			tmprepo=malloc(sizeof(*tmprepo));
			tmprepo->fusedgit=fusedgit;
			tmprepo->repo=repo;
			tmprepo->repoPath=strdup(repoPath);
			tmprepo->odb=odb;
			tmprepo->entries=0;
			tmprepo->next=NULL;
		} else {
			git_repository_free(repo);
		}
	}
	return tmprepo;
}

static int parsePath(const char *treeish, const char *inpath, const git_oid *oid, char *outpath, int opathlen)
{
	int ii=0, oi=0;
	int slash=1;
	char oidstr[GIT_OID_HEXSZ + 1];
	git_oid_tostr(oidstr, sizeof(oidstr), oid);
	const char *append=NULL;
	while (inpath[ii]) {
		char c=inpath[ii];
		if (c=='<') {
			if (strncmp(inpath + ii, "<hash>", 6)==0) {
				append=oidstr;
				ii+=5;
			} else if (strncmp(inpath + ii, "<treeish>", 9)==0) {
				append=treeish;
				ii+=8;
			} else {
				printf("Error parsing path: unknown tag, supported tags are <hash> or <treeish>\n");
				return -1;
			}
		}
		if (c=='\\' || c=='/')
			c='/';
		if (!append) {
			if (oi<opathlen && (c!='/' || !slash))
				outpath[oi++]=c;
		} else {
			int ai=0;
			while (append[ai]) {
				if (oi<opathlen)
					outpath[oi++]=append[ai];
				ai++;
			}
			append=NULL;
		}
		slash=(c=='/');
		ii++;
	}
	if (oi>=opathlen) {
		outpath[opathlen - 1]='\0';
		return -1;
	}
	outpath[oi]='\0';
	--oi;
	if (outpath[oi]=='/')
		outpath[oi]='\0';
	return 0;
}

struct addSubmodParam
{
	fusedgit_repo_t repo;
	const char *outPath;
	int recurse;
};

static int addtree_byObj(fusedgit_repo_t tmprepo, git_object *obj, const char *path, int submodules);

static int addSubmodCb(git_submodule *sm, const char *name, void *payload)
{
	struct addSubmodParam *p=payload;
	const char *path=git_submodule_path(sm);
	const git_oid *oid=git_submodule_head_id(sm);
	if (path && oid) {
		size_t repoPathLen=strlen(p->repo->repoPath);
		size_t len=repoPathLen + strlen(path) + 2;
		char *subpath=malloc(len);
		strcpy(subpath, p->repo->repoPath);
		strcat(subpath, "/");
		strcat(subpath, path);
		printf("Adding submodule %s\n", subpath);
		fusedgit_repo_t subrepo=fusedgit_addrepo(p->repo->fusedgit, subpath);

		if (subrepo) {
			git_object *obj;
			if (!git_object_lookup(&obj, subrepo->repo, oid, GIT_OBJ_ANY)) {
				len=strlen(p->outPath) + strlen(path) + 2;
				char *outpath=malloc(len);
				strcpy(outpath, p->outPath);
				if (outpath[0])
					strcat(outpath, "/");
				strcat(outpath, path);
				printf("Mapping submodule to %s\n", outpath);
				if (addtree_byObj(subrepo, obj, outpath, p->recurse ? 2 : 0))
					printf("Mapping submodule failed\n");
				free(outpath);
				git_object_free(obj);
			}
			fusedgit_releaserepo(subrepo);
		} else {
			printf("Adding submodule failed\n");
		}
		free(subpath);
	}
	return 0;
}

static int addtree_byObj(fusedgit_repo_t tmprepo, git_object *obj, const char *path, int submodules)
{
	int res=-1;
	git_tree  *tObj=NULL;
	if (git_object_peel((git_object **)&tObj, obj, GIT_OBJ_TREE))
		goto error_exit_0;

	const git_oid *oid=git_tree_id(tObj);

	fusedgit_t fgit=tmprepo->fusedgit;

	struct fsEntry *baseDir=NULL;
	const char *resolv=path;
	if (fgit->rootEntry) {
		resolv=resolveDirEntry(&baseDir, fgit->rootEntry, path);
		if (!resolv) {
			if (baseDir->virtCnt<0) {
				printf("Cannot mount - path is file: %s\n", path);
				res=-1;
				goto error_exit_free;
			}
			if ( baseDir->uniq) {
				printf("Cannot mount - path already mounted: %s\n", path);
				res=-1;
				goto error_exit_free;
			}
			// assert (!baseDir->repo
			baseDir->repo=tmprepo->repo;
			baseDir->odb=tmprepo->odb;
			allocUniq(baseDir, oid);
			tmprepo->entries++;
			goto normal_exit;
		}
	}
	char *wrResolv=strdup(resolv);
	char *nextDir=wrResolv;
	char *slash;
	struct fsEntry *firstNewEntry=NULL;
	struct fsEntry *prevEntry=NULL;
	while ((slash=strchr(nextDir, '/'))) {
		*slash='\0';
		struct fsEntry *virt=allocFsEntry(nextDir, fgit, NULL, NULL, NULL, 0);
		virt->virtCnt=1;
		virt->virtChilds=malloc(sizeof(struct fsEntry *));
		if (prevEntry)
			prevEntry->virtChilds[0]=virt;
		prevEntry=virt;
		if (!firstNewEntry)
			firstNewEntry=virt;
		nextDir=slash + 1;
	}
	struct fsEntry *entry=allocFsEntry(nextDir, fgit, tmprepo->repo, tmprepo->odb, oid, 0);
	tmprepo->entries++;
	if (prevEntry)
		prevEntry->virtChilds[0]=entry;
	if (!firstNewEntry)
		firstNewEntry=entry;

	if (!fgit->rootEntry) {
		if (!nextDir[0]) {
			fgit->rootEntry=firstNewEntry;
		} else {
			fgit->rootEntry=allocFsEntry("", fgit, NULL, NULL, NULL, 0);
			baseDir=fgit->rootEntry;
		}
	}
	if (baseDir) {
		baseDir->virtCnt++;
		baseDir->virtChilds=realloc(baseDir->virtChilds, sizeof(struct fsEntry *) * baseDir->virtCnt);
		baseDir->virtChilds[baseDir->virtCnt - 1]=firstNewEntry;
	}
	free(wrResolv);
	if (submodules) {
		struct addSubmodParam p = {
			.repo=tmprepo,
			.recurse=(submodules==2),
			.outPath=path
		};
		git_submodule_foreach(tmprepo->repo, addSubmodCb, &p);
	}

normal_exit:
	res=0;

error_exit_free:
	git_tree_free(tObj);
error_exit_0:
	return res;
}

int fusedgit_addtree(fusedgit_repo_t tmprepo, const char *treeish, const char *inpath, int submodules)
{
	int res=-1;
	git_object *obj=NULL;

	if (!git_revparse_single(&obj, tmprepo->repo, treeish)) {
		const git_oid *oid=git_object_id(obj);
		char path[1000];
		if (!parsePath(treeish, inpath, oid, path, 900))
			res=addtree_byObj(tmprepo, obj, path, submodules);
		git_object_free(obj);
	}

	return res;
}

void fusedgit_releaserepo(fusedgit_repo_t tmprepo)
{
	if (tmprepo) {
		if (!tmprepo->entries) {
			git_odb_free(tmprepo->odb);
			git_repository_free(tmprepo->repo);
			free(tmprepo->repoPath);
			free(tmprepo);
		} else {
			git_odb_free(tmprepo->odb);
			tmprepo->odb=NULL;

			struct fusedgit *fgit=tmprepo->fusedgit;
			if (fgit->lastRepo) {
				fgit->lastRepo->next=tmprepo;
				fgit->lastRepo=tmprepo;
			} else {
				fgit->lastRepo=fgit->firstRepo=tmprepo;
			}
		}
	}
}
