#include "dfs_memory.h"
/*
 *
 <1>alloca是向栈申请内存,因此无需释放.
 <2>malloc分配的内存是位于堆中的,并且没有初始化内存的内容,因此基本上malloc之后,调用函数memset来初始化这部分的内存空间.
 <3>calloc则将初始化这部分的内存,设置为0.
 * */
void * memory_alloc(size_t size)
{
    void *p = nullptr;
    
    if (size == 0) 
	{
        return nullptr;
    }
	
    p = malloc(size);   
	
    return p;
}

// 申请内存并初始化为 0
void * memory_calloc(size_t size)
{
    void *p = memory_alloc(size);

    if (p) 
	{
        memory_zero(p, size);
    }
    
    return p;
}

void memory_free(void *p)
{
    if (p) 
	{
        free(p);
    }
}

// 内存对其的分配
void * memory_memalign(size_t alignment, size_t size)
{
    void *p = nullptr;

    if (size == 0) 
	{
        return nullptr;
    }
	// 分配size大小的字节，并将分配的内存地址存放在memptr中。分配的内存的地址将是alignment的倍数
    posix_memalign(&p, alignment, size);
    
    return p;
}

int memory_n2cmp(uchar_t *s1, uchar_t *s2, size_t n1, size_t n2)
{
    size_t n = 0;
    int    m = 0, z = 0;

    if (n1 <= n2) 
	{
        n = n1;
        z = -1;
    } 
	else 
	{
        n = n2;
        z = 1;
    }
	
    m = memory_memcmp(s1, s2, n);
    if (m || n1 == n2) 
	{
        return m;
    }
	
    return z;
}

