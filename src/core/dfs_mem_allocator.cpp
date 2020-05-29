#include "dfs_mem_allocator.h"
#include "dfs_shmem_allocator.h"
#include "dfs_mempool_allocator.h"

dfs_mem_allocator_t *dfs_mem_allocator_new(int allocator_type)
{
    dfs_mem_allocator_t *allocator = nullptr;
	
    allocator = (dfs_mem_allocator_t *)malloc(sizeof(dfs_mem_allocator_t));
    if (!allocator) 
	{
        return nullptr;
    }
	
    switch (allocator_type) 
	{
        case DFS_MEM_ALLOCATOR_TYPE_SHMEM:
            *allocator = *dfs_get_shmem_allocator();
            break;
			
        case DFS_MEM_ALLOCATOR_TYPE_MEMPOOL:
            *allocator = *dfs_get_mempool_allocator();
            break;
			
       case DFS_MEM_ALLOCATOR_TYPE_COMMPOOL:
            *allocator = *dfs_get_commpool_allocator();
 
        default:
            return nullptr;
    }
	
    allocator->private_data = nullptr;

    return allocator;
}

dfs_mem_allocator_t *dfs_mem_allocator_new_init(int allocator_type,
                                                          void *init_param)
{
    dfs_mem_allocator_t *allocator = nullptr;

    allocator = (dfs_mem_allocator_t *)malloc(sizeof(dfs_mem_allocator_t));
    if (!allocator) 
	{
        return nullptr;
    }
	
    switch (allocator_type) 
	{
        case DFS_MEM_ALLOCATOR_TYPE_SHMEM:
            *allocator = *dfs_get_shmem_allocator();
            break;
			
        case DFS_MEM_ALLOCATOR_TYPE_MEMPOOL:
            *allocator = *dfs_get_mempool_allocator();
            break;
			
        case DFS_MEM_ALLOCATOR_TYPE_COMMPOOL: //
            *allocator = *dfs_get_commpool_allocator();
            break;
			
        default:
            return nullptr;
    }
	
    allocator->private_data = nullptr;
    if (init_param && allocator->init(allocator, init_param)
        == DFS_MEM_ALLOCATOR_ERROR) 
    {
        return nullptr;
    }
	
    return allocator;
}

void dfs_mem_allocator_delete(dfs_mem_allocator_t *allocator)
{
    dfs_shmem_allocator_param_t   shmem;
    dfs_mempool_allocator_param_t mempool;

    if (!allocator) 
	{
        return;
    }
	
    if (allocator->private_data) 
	{
        switch (allocator->type) 
		{
            case DFS_MEM_ALLOCATOR_TYPE_SHMEM:
                allocator->release(allocator, &shmem);
                break;
				
            case DFS_MEM_ALLOCATOR_TYPE_MEMPOOL:
                allocator->release(allocator, &mempool);
                break;
				
            case DFS_MEM_ALLOCATOR_TYPE_COMMPOOL:
                allocator->release(allocator, nullptr);
                break;
        }
    }

    free(allocator);
}

