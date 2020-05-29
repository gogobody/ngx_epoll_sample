#ifndef DFS_MBLKS_H 
#define DFS_MBLKS_H

#include <stdlib.h>

#include "dfs_lock.h"

#define MEM_BLOCK_HEAD          sizeof(struct mem_data)
#define SIZEOF_PER_MEM_BLOCK(X) ((X) + MEM_BLOCK_HEAD)

#define LOCK_INIT(x) do  { \
    dfs_atomic_lock_init(x); \
} while(0)

#define TRY_LOCK(x) do  { \
    dfs_lock_errno_t error; \
    dfs_atomic_lock_try_on((x), &error); \
} while(0)

#define LOCK(x) do {\
    dfs_lock_errno_t error; \
    dfs_atomic_lock_on((x), &error); \
} while(0)


#define UNLOCK(x) do { \
    dfs_lock_errno_t error; \
    dfs_atomic_lock_off((x), &error); \
} while(0)

#define LOCK_DESTROY(x) {}

struct mem_data 
{
    void *next;
    char  data[0];
};

typedef struct mem_mblks_param_s 
{
    void *(*mem_alloc) (void *priv, size_t size); //allocator_malloc
    void (*mem_free) (void *priv,void *mem_addr); //allocator_free
    void *priv; // allocator
} mem_mblks_param_t;

// allocator 的 freeblks
struct mem_mblks 
{
    int                hot_count;
    int                cold_count; // 冷的可用的 mblks
    size_t             padded_sizeof_type; //SIZEOF_PER_MEM_BLOCK
    size_t             real_sizeof_type; // eg: sizeof(blk_info)
    void              *start_mblks;
    void              *end_mblks;
    mem_mblks_param_t  param; //mem_mblks_param_t
    dfs_atomic_lock_t  lock;
    struct mem_data   *free_blks; //fi_store_t
};

struct mem_mblks *mem_mblks_new_fn(size_t, int64_t, mem_mblks_param_t *);

#define mem_mblks_new(type, count, param) mem_mblks_new_fn(sizeof(type), count, param)

void *mem_get(struct mem_mblks*);
void *mem_get0(struct mem_mblks*);
void mem_put(void *);
void mem_mblks_destroy(struct mem_mblks *);

#endif

