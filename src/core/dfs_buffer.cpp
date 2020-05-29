#include "dfs_buffer.h"
#include "dfs_memory.h"
#include "dfs_memory_pool.h"

buffer_t * buffer_alloc(pool_t *pool)
{
    return (buffer_t *)pool_alloc(pool, sizeof(buffer_t));
}

buffer_t * buffer_create(pool_t *pool, size_t size)
{
    buffer_t *b = nullptr;

    if (size == 0) 
	{
        return nullptr;
    }
	
    if (!pool) 
	{
        b = (buffer_t *)memory_alloc(sizeof(buffer_t)); //先申请一个buf头部
        if (!b) //申请失败
		{
            return nullptr;
        }
		
        b->start = (uchar_t *)memory_alloc(size); //buf内容体
        if (!b->start) 
		{
            memory_free(b);
			
            return nullptr;
        }
		
		b->temporary = NGX_FALSE;
    } 
	else 
	{
        b = (buffer_t *)buffer_alloc(pool);
        if (!b) 
		{
            return nullptr;
        }
		
        b->start = (uchar_t *)pool_alloc(pool, size);
        if (!b->start) 
		{
            return nullptr;
        }
		
        b->temporary = NGX_TRUE;
    }
    //设置各类指针
    b->pos = b->start;
    b->last = b->start;
    b->end = b->last + size;
    b->memory = NGX_TRUE;
    b->in_file = NGX_FALSE;
	
    return b;
}
// 收缩 buffer , 相当于将pos之后的移动到start
void buffer_shrink(buffer_t* buf)
{
	int blen = 0;
	
	if (!buf) 
	{
        return;
    }
	
	if (buf->start == buf->pos) 
	{
		return;
	}
	
	blen = buffer_size(buf); // last - pos
	if (!blen) 
	{
		buffer_reset(buf);
		
		return;
	}
	
	memmove(buf->start, buf->pos, blen); // copy
	buf->pos = buf->start;
	buf->last = buf->pos+blen;
}

void buffer_free(buffer_t *buf)
{
    if (!buf || buf->temporary) 
	{
        return;
    }

    memory_free(buf->start);
    memory_free(buf);
}

