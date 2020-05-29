#ifndef DFS_CHAIN_H
#define DFS_CHAIN_H

#include "dfs_types.h"
#include "dfs_buffer.h"

#define DFS_CHAIN_ERROR (chain_t *) NGX_ERROR

// 链表
struct chain_s 
{
    buffer_t *buf; //buf指向当前的ngx_buf_t缓冲区
    chain_t  *next; //next则用来指向下一个ngx_chain_t，如果这是最后一个ngx_chain_t，则需要把next置为nullptr。
};

typedef int (*chain_output_filter_pt)(void *ctx, chain_t *in);

typedef struct chain_output_ctx_s 
{
    off_t    limit;
    pool_t  *pool;
    conn_t  *connection;
    chain_t *out; // chain 链表
	int      fd;
    uint32_t sendfile;
} chain_output_ctx_t;

chain_t *chain_alloc(pool_t *pool);
int      chain_reset(chain_t *cl);
int      chain_empty(chain_t *cl);
uint64_t chain_size(chain_t *in) ;
int      chain_output(chain_output_ctx_t *ctx, chain_t *in);
int      chain_output_with_limit(chain_output_ctx_t *ctx, 
	chain_t *in, size_t limit);
void     chain_append_withsize(chain_t **dst_chain,
    chain_t *src_chain, size_t size, chain_t **free_chain);
void     chain_append_all(chain_t **dst_chain, chain_t *src_chain);
int      chain_append_buffer(pool_t *pool, chain_t **dst_chain,
    buffer_t *src_buffer);
int      chain_append_buffer_withsize(pool_t *pool,
    chain_t **dst_chain, buffer_t *src_buffer, size_t size);
void     chain_read_update(chain_t *chain, size_t size);
chain_t *chain_write_update(chain_t *chain, size_t size);

#endif

