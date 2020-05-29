#include "dfs_memory_pool.h"
#include "dfs_memory.h"
#include "dfs_error_log.h"

#define DFS_ALIGNMENT sizeof(uint64_t)

static void *pool_alloc_block(pool_t *pool, size_t size);
static void *pool_alloc_large(pool_t *pool, size_t size);


//创建ngx的内存池

pool_t * pool_create(size_t size, size_t max_size, log_t *log)
{
    pool_t *p = nullptr;

    if (size == 0) 
	{
        return nullptr;
    }
	
    if (size < sizeof(pool_t)) 
	{
        size += sizeof(pool_t);
    }
	//执行内存分配
    p = (pool_t *)memory_alloc(size);
    if (!p) 
	{
        return nullptr;
    }
	//开始初始化数据区
	//由于刚刚的开辟内存，因此last指针指向数据区的开始。
    p->data.last = (uchar_t *) p + sizeof(pool_t); //?
    p->data.end = (uchar_t *) p + size; //end指针指向数据区的末尾
    p->data.next = nullptr;
	
    //当前内存池的大小减去内存池头部信息的大小，得到真正能够使用的内存大小
    size -= sizeof(pool_t);
	//设置max，因为内存池的大小不超过一页（4k），所以内存池的最大值也就是size和NGX_MAX_ALLOC_FROM_POOL之中较小的。
    p->max = (size < max_size) ? size : max_size;
	//current表示当前内存池
    p->current = p;
	//其他域都置成nullptr
    p->large = nullptr;
    p->log = log;
	//返回指针
    return p;
}


//重置内存池
void pool_reset(pool_t *pool)
{
    pool_t       *p = nullptr;
    pool_large_t *l = nullptr;
    
    if (!pool) 
	{
        return;
    }
	//释放大块内存
    for (l = pool->large; l; l = l->next) 
	{
        if (l->alloc) 
		{
            dfs_log_debug(pool->log, DFS_LOG_DEBUG, 0,
                "pool_reset: free pool large:%p, size:%uL, pool:%p",
                l->alloc, l->size, pool);

            memory_free(l->alloc);
            l->alloc = nullptr;
        }
    }
	
    p = pool;
    p->current = p;
	//重置小块内存区域
    while (p) 
	{
        dfs_log_debug(pool->log, DFS_LOG_DEBUG, 0,
            "pool_reset: reset pool:%p, size:%uL",
            p, (char *)p->data.end - ((char *)p));
		
        p->data.last = (uchar_t *)p + sizeof(pool_t);
        p = p->data.next;
    }
}

//销毁内存池
void pool_destroy(pool_t *pool)
{
    pool_t       *p = nullptr;
    pool_t       *n = nullptr;
    pool_large_t *l = nullptr;

    if (!pool) 
	{
        return;
    }
	//先从大块内存开始进行释放
    for (l = pool->large; l; l = l->next) 
	{
        if (l->alloc) 
		{
            dfs_log_debug(pool->log, DFS_LOG_DEBUG, 0,
                "pool_destroy: free pool large:%p, size:%uL, pool:%p",
                l->alloc, l->size, pool);

            memory_free(l->alloc);
        }
    }
	//最后释放内存池的内存池
    for (p = pool, n = pool->data.next; ; p = n, n = n->data.next) 
	{
        memory_free(p);
        if (!n) 
		{
            break;
        }
    }
}

//考虑内存对齐的内存池内存申请
void * pool_alloc(pool_t *pool, size_t size)
{
    pool_t  *p = nullptr;
    uchar_t *m = nullptr;

    if (!pool || (size == 0)) 
	{
        return nullptr;
    }
	//判断当前申请内存的大小是否超过max，没有则说明申请的是小块内存
    if (size <= pool->max) 
	{
        p = pool->current; //得到当前内存池指针
		
        while (p) 
		{
            m = (uchar_t *)dfs_align_ptr(p->data.last, DFS_ALIGNMENT);//就是将内存的地址对齐
            if ((m < p->data.end) && ((size_t) (p->data.end - m) >= size)) //尝试在已有的内存池节点中分配内存
			{
                p->data.last = m + size;//更新last指针，并将前面保存的last返回 使用该内存
				
                return m;
            }
			
            p = p->data.next;//不够就找下一个pool？
        }
		
        return pool_alloc_block(pool, size);//当前已有节点都分配失败，创建一个新的内存池节点
    }
	//申请大块内存
    return pool_alloc_large(pool, size);
}


// 内存对其并初始化
void * pool_calloc(pool_t *pool, size_t size)
{
    void *p = nullptr;

    p = pool_alloc(pool, size);
    if (p) 
	{
        memory_zero(p, size);
    }

    return p;
}

//实现主要就是重新分配一块内存池，然后链接到当前内存池的数据去指针。注意这里的新内存池大小是与其父内存池一样大。
static void * pool_alloc_block(pool_t *pool, size_t size)
{
    uchar_t  *nm = NULL; //new memory
    size_t    psize = 0;
    pool_t   *p = NULL;
    pool_t   *np = NULL;
    pool_t   *current = NULL;

    if (!pool || size == 0) 
	{
        return NULL;
    }
	//计算当前内存池的的大小
    psize = (size_t) (pool->data.end - (uchar_t *) pool);
    
    dfs_log_debug(pool->log, DFS_LOG_DEBUG, 0, 
        "pool_alloc: alloc next block size:%d, need size:%d", psize, size);

	//再开辟一块同样大小的内存池
    nm = (uchar_t *)memory_alloc(psize);
    if (!nm) 
	{
        return NULL;
    }
    np = (pool_t *) nm;
	//更新指针，初始化新开辟出来的内存池
    np->data.end = nm + psize;
    np->data.next = NULL;
	//由下面可以看出来，新开辟出来的内存池只存放了pool_data_t的信息
    nm += sizeof(pool_data_t);  //头部信息大小
    nm = dfs_align_ptr(nm, DFS_ALIGNMENT); //内存对齐操作
    np->data.last = nm + size;  //更新last指针的位置，即申请的size大小
    
    for (p = pool->current; p->data.next; p = p->data.next) //设置current
	{
        if ((size_t)(p->data.end - p->data.last) < DFS_ALIGNMENT) //假设老的内存池没有空间了，从比较新的内存块开始  提高效率
		{
            current = p->data.next; 
        }
    }
	//连接到最后一个内存池上
    p->data.next = np;
	//如果current为NULL，则current就为new_p
    pool->current = current ? current : np;
    
    return nm;
}

//大块内存的申请，就是简单的malloc一块大块内存链接到主内存池上。
static void * pool_alloc_large(pool_t *pool, size_t size)
{
    void         *p = NULL;
    pool_large_t *large = NULL;

    if (!pool || (size == 0)) 
	{
        return NULL;
    }
    
    dfs_log_debug(pool->log, DFS_LOG_DEBUG, 0, 
        "pool_alloc: alloc large size:%d", size);

    p = memory_alloc(size);
    if (!p ) 
	{
        return NULL;
    }
	//重新创建一个
    large = (pool_large_t *)pool_alloc(pool, sizeof(pool_large_t));
    if (!large) 
	{
        memory_free(p);
        return NULL;
    }
	//链接数据区指针p到large。这里可以看到直接插入到large链表的头的
    large->alloc = p; //数据指针
    large->size = size;
    large->next = pool->large;
    pool->large = large;
    
    return p;
}
//分配内存，返回内存地址按alignment对齐，内存未作任何初始化
void * pool_memalign(pool_t *pool, size_t size, size_t alignment)
{
    void         *p = NULL;
    pool_large_t *large = NULL;

	/*用户指定alignment对齐要求，直接从系统中分配large内存块*/
    p = memory_memalign(alignment, size);
    if (!p) 
	{
        return NULL;
    }
	/*直接从block中分配管理large内存块的内存，
    * 为什么不像ngx_palloc_large那样先查找有无可用的管理内存块?*/
    large = (pool_large_t *)pool_alloc(pool, sizeof(pool_large_t));
    if (!large) 
	{
        memory_free(p);
        return NULL;
    }
	
    large->alloc = p;
    large->size = size;
    large->next = pool->large;
    pool->large = large;
    
    return p;
}

size_t dfs_align(size_t d, uint32_t a)
{
    if (d % a) 
	{
        d += a - (d % a);
    }

    return d;
}

