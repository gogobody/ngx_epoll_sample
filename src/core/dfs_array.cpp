#include "dfs_array.h"
#include "dfs_memory.h"


//从内存池中分配内存用于创建ngx_array_t对象。
void * array_create(pool_t *p, uint32_t n, size_t size)
{
    array_t *a = nullptr;

    if ((size == 0) || (n == 0)) 
	{
        return nullptr;
    }
    
    if (p) 
	{
        a = (array_t *)pool_alloc(p, sizeof(array_t));
        a->elts = pool_alloc(p, n * size);
    } 
	else 
	{
        a = (array_t *)memory_alloc(sizeof(array_t));
        a->elts = memory_alloc(n * size);
    }
    
    if (!a || !(a->elts)) 
	{
        return nullptr;
    }

    a->nelts = 0; //已使用的元素个数
    a->size = size;
    a->nalloc = n; //整个数组长度
    a->pool = p;

    return a;
}
//从内存池中分配n*size大小的内存，其首地址保存在elts指针中
int array_init(array_t *array, pool_t *pool, uint32_t n, size_t size)
{
    if (!array || (n == 0) || (size == 0)) 
	{
        return NGX_ERROR;
    }
	
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;
	
    if (pool) 
	{
        array->elts = pool_alloc(pool, n * size);
    } 
	else
	{
        array->elts = memory_alloc(n * size);
    }
	
    if (!array->elts) 
	{
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

void array_reset(array_t *a)
{
    memset(a->elts, 0, a->size * a->nelts);
    a->nelts = 0;
}
//释放ngx_array_t在内存池中占用的空间
void array_destroy(array_t *a)
{
    pool_t *p = nullptr;

    if (!a) 
	{
        return;
    }
    
    p = a->pool;
    
    if (!p) 
	{
        memory_free(a->elts);
		
        return;
    }
    
    if ((uchar_t *) a->elts + a->size * a->nalloc == p->data.last) 
	{
        p->data.last = (uchar_t *)a->elts;
    }

    if ((uchar_t *) a + sizeof(array_t) == p->data.last) 
	{
        p->data.last = (uchar_t *) a;
    }
}

//从ngx_array_t对象中再分配一个元素，如果ngx_array_t中没有可用的空闲空间则从ngx_pool_t中申请
void * array_push(array_t *a)
{
    void   *elt = nullptr;
    void   *na = nullptr;
    size_t  size = 0;
    pool_t *p = nullptr;

    if (!a) 
	{
        return nullptr;
    }
    
    p = a->pool;
	
    // the array is full, note: array need continuous space
    //ngx_array_t空闲元素已使用完
    if (a->nelts == a->nalloc) 
	{
        size = a->size * a->nalloc;
		
        if (!p) 
		{
            return nullptr;
        }
		
        // the array allocation is the last in the pool
        // 数组分配是池中的最后一个
        // and there is continuous space for it
        if ((uchar_t *) a->elts + size == p->data.last
            && p->data.last + a->size <= p->data.end) 
        {
            p->data.last += a->size;
            a->nalloc++;
        } //从同一个连续空间中申请
		else 
		{
            // allocate a new array
            //同一个连续空间大小不够，则重新申请两倍的ngx_array_t大小，并将之前的元素拷贝过来
            //注：这里好像没有把之前ngx_array_t的空间释放
            if (p) 
			{
                na = pool_alloc(p, 2 * size);
                if (!na) 
				{
                    return nullptr;
                }
				
                memory_memcpy(na, a->elts, size);
                a->elts = na;
                a->nalloc *= 2;
            } 
			else 
			{
                na = memory_realloc(a->elts, 2 * size);
                if (!na) 
				{
                    return nullptr;
                }
				
                a->elts = na;
                a->nalloc *= 2;
            }
        }
    }
	
    elt = (uchar_t *) a->elts + a->size * a->nelts;
    a->nelts++;
    
    return elt;
}

//数组继承?
void array_inherit(void *pool, array_t *a1, array_t *a2, 
	                  func_cmp func, func_inherit func2)
{
    size_t i = 0, j = 0;
    void  *v1 = nullptr, *v2 = nullptr;

    for (i = 0; i < a1->nelts; i++) 
	{
        v1 = (void *) ((char *)a1->elts + a1->size * i);
		
        for (j = 0; j < a2->nelts; j++)
		{
            v2 = (void *) ((char *)a2->elts + a2->size * i);
			
            if (func(v1, v2)) 
			{
                func2(pool, v1, v2);
				
				break;
            }
        }
	}
}

// find
void * array_find(array_t *array, void *dst, array_cmp_func func)
{
	size_t i = 0;
	void  *v = nullptr;
	
	for (i = 0; i < array->nelts; i++) 
	{
		v = (void*)((char *)array->elts + array->size * i);
		
		if (func(v, dst)) 
		{
            return v;
        }
    }
	
    return nullptr;
}

