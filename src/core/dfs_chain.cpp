#include "dfs_chain.h"
#include "dfs_conn.h"

//缓冲区链表
chain_t * chain_alloc(pool_t *pool)
{
    chain_t *cl = nullptr;

    if (!pool) 
	{
        return nullptr;
    }
	
    cl = (chain_t *)pool_alloc(pool, sizeof(chain_t));
    if (!cl) 
	{
        return nullptr;
    }
	
    cl->next = nullptr;

    return cl;
}

int chain_reset(chain_t *cl)
{
    for (; cl; cl = cl->next) 
	{
        cl->buf->pos = cl->buf->last = cl->buf->start;
    }
  
    return NGX_OK;
}

int chain_empty(chain_t *cl)
{
    for (; cl; cl = cl->next) 
	{
        if (buffer_size(cl->buf) > 0) 
		{
            return NGX_FALSE;
        }
    }
  
    return NGX_TRUE;
}

uint64_t chain_size(chain_t *in) 
{
    uint64_t len = 0;

    while (in) 
	{
        len += buffer_size(in->buf);
        in = in->next;
    }

    return len;
}

int chain_output(chain_output_ctx_t *ctx, chain_t *in)
{
    conn_t *c = nullptr;
    
    if (!ctx) 
	{
        return NGX_ERROR;
    }
	
    if (in) 
	{
        chain_append_all(&ctx->out, in);
    }
	
    if (chain_empty(ctx->out)) 
	{
        return NGX_OK;
    }
	
    c = ctx->connection;
    if (!c) 
	{
        return NGX_ERROR;
    }

	while (c->write->ready && ctx->out) 
	{
	    if (ctx->out->buf->memory) 
		{
			//sysio_writev_chain
	        ctx->out = c->send_chain(c, ctx->out, ctx->limit);
	    } 
		else 
		{
	        //sysio_sendfile_chain
	        ctx->out = c->sendfile_chain(c, ctx->out, ctx->fd, ctx->limit);
	    }
		
	    if (ctx->out == DFS_CHAIN_ERROR) 
		{
        
	        return NGX_ERROR;
	    }
	}

	if (ctx->out) 
	{
		return NGX_AGAIN;
	}
	
    return NGX_OK;
}

int chain_output_with_limit(chain_output_ctx_t *ctx, chain_t *in, 
	                                size_t limit)
{
    conn_t *c = nullptr;
    size_t sent = 0;
    int64_t cur_limit = limit;
    
    c = ctx->connection;
    sent = c->sent;
	
    while (c->write->ready && ctx->out) 
	{
        if (ctx->out->buf->memory) 
		{
            //sysio_writev_chain
            ctx->out = c->send_chain(c, ctx->out, cur_limit);
        } 
		else 
		{
            //sysio_sendfile_chain
            ctx->out = c->sendfile_chain(c, ctx->out, ctx->fd, cur_limit);
        }
        
        if (ctx->out == DFS_CHAIN_ERROR || !c->write->ready) 
		{
            break;
        }
        
        if (limit) 
		{
            cur_limit -= c->sent - sent;
            sent = c->sent;
			
            if (cur_limit <= 0) 
			{
                break;
            }
        }
    }
    
    if (ctx->out == DFS_CHAIN_ERROR) 
	{
        return NGX_ERROR;
    }
    
	if (ctx->out) 
	{
		return NGX_AGAIN;
	}
	
    return NGX_OK;
}

void chain_append_withsize(chain_t **dst_chain, chain_t *src_chain, 
	                                size_t size, chain_t **free_chain)
{
    size_t buf_size = 0;
    
    if (!dst_chain || !src_chain || size == 0) 
	{
        return;
    }
	
    while (*dst_chain) 
	{
        dst_chain = &((*dst_chain)->next);
    }
	
    while (src_chain && size > 0) 
	{
        buf_size = buffer_size(src_chain->buf);
        *dst_chain = src_chain;
        src_chain = src_chain->next;
        (*dst_chain)->next = nullptr;
        dst_chain = &((*dst_chain)->next);
        size -= buf_size;
    }
	
    // put other chain into free_chain
    if (src_chain) 
	{
        *free_chain = src_chain;
    }
}

void chain_append_all(chain_t **dst_chain, chain_t *src_chain)
{
    if (!dst_chain || !src_chain) 
	{
        return;
    }
	
    while (*dst_chain) 
	{
        dst_chain = &((*dst_chain)->next);
    }
	
    *dst_chain = src_chain;
}

int chain_append_buffer(pool_t *pool, chain_t **dst_chain,
                                buffer_t *src_buffer)
{
    chain_t *ln = nullptr;
    
    if (!pool || !dst_chain || !src_buffer) 
	{
        return NGX_OK;
    }
	
    while (*dst_chain) 
	{
        dst_chain = &((*dst_chain)->next);
    }
	
    ln = chain_alloc(pool);
    if (!ln) 
	{
        return NGX_ERROR;
    }
	
    ln->buf = src_buffer;
    *dst_chain = ln;

    return NGX_OK;
}

int chain_append_buffer_withsize(pool_t *pool, chain_t **dst_chain, 
	                                        buffer_t *src_buffer, size_t size)
{
    chain_t *ln = nullptr;
    
    if (!pool || !dst_chain || !src_buffer) 
	{
        return NGX_OK;
    }
	
    while (*dst_chain) 
	{
        dst_chain = &((*dst_chain)->next);
    }
	
    ln = chain_alloc(pool);
    if (!ln) 
	{
        return NGX_ERROR;
    }
	
    ln->buf = src_buffer;
    *dst_chain = ln;

    return NGX_OK;
}

void chain_read_update(chain_t *chain, size_t size)
{
    size_t chain_size = 0;
    
    if (!chain) 
	{
        return;
    }
	
    while (chain && size > 0) 
	{
        chain_size = chain->buf->end - chain->buf->last;
		
        if (size >= chain_size) 
		{
            chain->buf->last = chain->buf->end;
            size -= chain_size;
        } 
		else 
		{
            chain->buf->last += size;
            size = 0;
        }
		
        chain = chain->next;
    }
}

// 发送之后更新 buf的pos
chain_t * chain_write_update(chain_t *chain, size_t size)
{
    size_t bsize = 0;
    
    while (chain && size > 0) 
	{
        bsize = buffer_size(chain->buf);
        if (size < bsize)
		{
			if (chain->buf->memory == NGX_TRUE) //说明发送出去的最后一字节数据的下一字节数据在in->buf->pos+send位置，下次从这个位置开始发送
			{
	            chain->buf->pos += size;//这块内存没有完全发送完毕，悲剧，下回得从这里开始。
			} 
			else 
			{
				chain->buf->file_pos += size;
			}
			
            return chain;
        }

        //说明该in->buf数据已经全部发送出去
        size -= bsize; //标记后面还有多少数据是我发送过的


        if (chain->buf->memory == NGX_TRUE) //说明该in->buf数据已经全部发送出去
		{
            chain->buf->pos = chain->buf->last;
		} 
		else 
		{
			chain->buf->file_pos = chain->buf->file_last;
		}
		
        chain = chain->next;
    }

    return chain;
}

