#ifndef DN_MODULE_H
#define DN_MODULE_H

#include "dfs_string.h"
#include "dn_cycle.h"
#include "dn_thread.h"

enum 
{
    PROCESS_MOD_INIT ,
    PROCESS_MOD_FREE 
};

typedef struct dfs_module_s dfs_module_t;

struct dfs_module_s 
{
    string_t  name;  // model name 
    int       index; //全部模块中的索引
    int       flag;  // process mode flag
    void     *data;
    int       (*master_init)(cycle_t *cycle); //初始化master
    int       (*master_release)(cycle_t *cycle); 
    int       (*worker_init)(cycle_t *cycle); 
    int       (*worker_release)(cycle_t *cycle);
	int       (*worker_thread_init)(dfs_thread_t *thread);//初始化线程
    int       (*worker_thread_release)(dfs_thread_t *thread);
};

void dfs_module_setup(void);
int dfs_module_master_init(cycle_t *cycle);
int dfs_module_master_release(cycle_t *cycle);
int dfs_module_woker_init(cycle_t *cycle);
int dfs_module_woker_release(cycle_t *cycle);
int dfs_module_workethread_init(dfs_thread_t *thread);
int dfs_module_wokerthread_release(dfs_thread_t *thread);

#endif

