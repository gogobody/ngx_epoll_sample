#ifndef DFS_MEMORY_POOL_H
#define DFS_MEMORY_POOL_H

#include "dfs_types.h"

#define DEFAULT_PAGESIZE       4096 //512B字节 4K
#define DEFAULT_CACHELINE_SIZE 64 //8字节

//内存池的数据区最大容量
#define DFS_MAX_ALLOC_FROM_POOL ((size_t)(DEFAULT_PAGESIZE - 1))
//内存地址对齐内存对齐操作，是一个内存地址取整宏，毕竟这里只是对指针进行偏移，last指针的位置需要自己动手将其调整。因为内存不对齐的话，会导致CPU I/O的次数增加，效率降低

#define dfs_align_ptr(p, a)\
    (uchar_t *) (((uintptr_t)(p) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))

typedef struct pool_large_s  pool_large_t;


//大块内存的信息,next指向下一块内存，alloc指向数据。
struct pool_large_s 
{
    void         *alloc;  //数据块指针地址
    size_t        size;
    pool_large_t *next; //指向下一个存储地址
};

//小块内存的信息
typedef struct pool_data_s 
{
    uchar_t *last;  //内存池中未使用内存的开始结点地址，最新的
    uchar_t *end;   //内存池的结束地址
    pool_t  *next;  //下一个内存池
} pool_data_t;  //last到end就是当前内存池还未使用的内存大小

//内存池的头部信息
struct pool_s 
{
    pool_data_t   data;    // pool's list used status(used for pool alloc) //数据区域
    size_t        max;     // max size pool can alloc  //最大每次可分配内存
    pool_t       *current; // current pool of pool's list  //指向当前的内存池指针地址
    pool_large_t *large;   // large memory of pool //存大块数据的链表
    log_t        *log;
};
//这个函数是内存池的创建函数。 第一个参数是内存池的大小（一次最大可申请的小块空间大小），其实实际的小块空间单次最大可申请大小还需要用size减去sizeof（ngx_pool_t）（内存池头部结构体的大小）
pool_t *pool_create(size_t size, size_t max_size, log_t *log);
void    pool_destroy(pool_t *pool);
void   *pool_alloc(pool_t *pool, size_t size); //考虑内存对齐的内存池内存申请
void   *pool_calloc(pool_t *pool, size_t size); //内存对其并初始化
void   *pool_memalign(pool_t *pool, size_t size, size_t alignment);//分配内存，返回内存地址按alignment对齐，内存未作任何初始化
void    pool_reset(pool_t *pool);
size_t  dfs_align(size_t d, uint32_t a);

#endif

