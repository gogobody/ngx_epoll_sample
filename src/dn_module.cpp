#include "dn_module.h"
#include "dn_error_log.h"

static int dfs_mod_max = 0;
/*
string_t  name;  // model name 
int 	  index; //全部模块中的索引
int 	  flag;  // process mode flag
void	 *data;
int 	  (*master_init)(cycle_t *cycle); //初始化master
int 	  (*master_release)(cycle_t *cycle); 
int 	  (*worker_init)(cycle_t *cycle); 
int 	  (*worker_release)(cycle_t *cycle);
int 	  (*worker_thread_init)(dfs_thread_t *thread);//初始化线程
int 	  (*worker_thread_release)(dfs_thread_t *thread);

*/
dfs_module_t dfs_modules[] = 
{
    // please miss the beginning;
    {
        string_make("errlog"),
        0,
        PROCESS_MOD_INIT,
        nullptr,
        dn_error_log_init,
        dn_error_log_release,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    },


    {string_null, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
};

// 初始化模块 index
void dfs_module_setup(void)
{
    int i = 0;
	
    for (i = 0; dfs_modules[i].name.data != nullptr; i++)
	{
        dfs_modules[i].index = dfs_mod_max++;
    }
}

// 初始化每个模块的master_init函数
int dfs_module_master_init(cycle_t *cycle)
{
    int i = 0;
	
    for (i = 0; i < dfs_mod_max; i++) 
	{
        if (dfs_modules[i].master_init != nullptr &&
            dfs_modules[i].master_init(cycle) == NGX_ERROR)
        {
            printf("process_master_init: module %s init failed\n",
                dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }

        dfs_modules[i].flag = PROCESS_MOD_FREE;
    }
	
    return NGX_OK;
}

int dfs_module_master_release(cycle_t *cycle)
{
    int i = 0;
	
    for (i = dfs_mod_max - 1; i >= 0; i--) 
	{
        if (dfs_modules[i].flag != PROCESS_MOD_FREE  ||
            dfs_modules[i].master_release == nullptr)
        {
            continue;
        }
		
        if (dfs_modules[i].master_release(cycle) == NGX_ERROR)
		{
            dfs_log_error(cycle->error_log, DFS_LOG_ERROR, 0,
                "%s deinit fail \n",dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }
		
        dfs_modules[i].flag = PROCESS_MOD_INIT;
    }
	
    return NGX_OK;
}

// set flag
// dn_data_storage_worker_init
int dfs_module_woker_init(cycle_t *cycle)
{
    int i = 0;
	
    for (i = 0; i < dfs_mod_max; i++) 
	{
        if (dfs_modules[i].worker_init != nullptr &&
            dfs_modules[i].worker_init(cycle) == NGX_ERROR)
        {
            dfs_log_error(cycle->error_log, DFS_LOG_ERROR, 0,
                "dfs_module_init_woker: module %s init failed\n",
                dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }
    }
	
    return NGX_OK;
}

//do nothing
int dfs_module_woker_release(cycle_t *cycle)
{
    int i = 0;
	
    for (i = 0; i < dfs_mod_max; i++) 
	{
        if (dfs_modules[i].worker_release!= nullptr &&
            dfs_modules[i].worker_release(cycle) == NGX_ERROR)
        {
            dfs_log_error(cycle->error_log, DFS_LOG_ERROR, 0,
                "dfs_module_init_woker: module %s init failed\n",
                dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }
    }
	
    return NGX_OK;
}


// dn_data_storage_thread_init
int dfs_module_workethread_init(dfs_thread_t *thread)
{
    int i = 0;
	
    for (i = 0; i < dfs_mod_max; i++) 
	{
        if (dfs_modules[i].worker_thread_init != nullptr &&
            dfs_modules[i].worker_thread_init(thread) == NGX_ERROR)
        {
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ERROR, 0,
                "dfs_module_init_woker: module %s init failed\n",
                dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }
    }
	
    return NGX_OK;
}

//do nothing
int dfs_module_wokerthread_release(dfs_thread_t *thread)
{
    int i = 0;

    for (i = 0; i < dfs_mod_max; i++) 
	{
        if (dfs_modules[i].worker_thread_release != nullptr &&
            dfs_modules[i].worker_thread_release(thread) == NGX_ERROR)
        {
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ERROR, 0,
                "dfs_module_init_woker: module %s init failed\n",
                dfs_modules[i].name.data);
			
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

