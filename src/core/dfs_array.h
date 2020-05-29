#ifndef DFS_ARRAY_H
#define DFS_ARRAY_H

#include "dfs_types.h"
#include "dfs_string.h"

struct array_s 
{ 
    void     *elts; //数组首地址 element 
    uint32_t  nelts; //已使用的元素个数
    size_t    size; //每个元素的大小
    uint32_t  nalloc; //整个数组长度
    pool_t   *pool; //数组所在的内存池
};

typedef int (*array_cmp_func)(void *, void *);
typedef void (*func_inherit)(void *, void *,void *);
typedef int (*func_cmp)(void *, void *);
//从内存池中分配内存用于创建ngx_array_t对象。
void *array_create(pool_t *p, uint32_t n, size_t size);
int   array_init(array_t *array, pool_t *pool, uint32_t n, size_t size);
void  array_reset(array_t *a);
void  array_destroy(array_t *a);
void *array_push(array_t *a);
void *array_find(array_t *array, void* dst, array_cmp_func);

#endif

