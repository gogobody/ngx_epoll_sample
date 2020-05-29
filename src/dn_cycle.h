#ifndef DN_CYCLE_H
#define DN_CYCLE_H

#include "dfs_types.h"
#include "dfs_string.h"
#include "dfs_array.h"
#include "dfs_memory_pool.h"

typedef struct cycle_s  //作为一个全局变量指向nginx当前运行的上下文环境
{
    void      *sconf;
    pool_t    *pool; //内存池  
    log_t     *error_log;
    array_t    listening_for_cli; // listening array
	char       listening_ip[32]; // dn ip // nouse now
	int 	   listening_port; // dn port
    string_t   conf_file;  //配置文件
	void      *cfs; //配置上下文数组(含所有模块) ？// contain io processfunc in dfs_setup
} cycle_t;

extern cycle_t *dfs_cycle;

cycle_t  *dn_cycle_create();
int       dn_cycle_init(cycle_t *cycle);
int       dn_cycle_free(cycle_t *cycle);
array_t  *cycle_get_listen_for_cli();
int       cycle_check_sys_env(cycle_t *cycle);

#endif

