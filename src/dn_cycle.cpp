#include "dn_cycle.h"
#include "dfs_conf.h"
#include "dfs_memory.h"
#include "dn_conf.h"
#include "dn_time.h"
#include "dn_error_log.h"

#define CYCLE_POOL_SIZE 16384

cycle_t         *dfs_cycle;  
extern string_t  config_file;

cycle_t *dn_cycle_create()
{
    cycle_t *cycle = nullptr;
    cycle = (cycle_t *)memory_calloc(sizeof(cycle_t));
    
    if (!cycle) 
	{
        printf("create cycle faild!\n");
		
        return cycle;
    }
    
    cycle->pool = pool_create(CYCLE_POOL_SIZE, CYCLE_POOL_SIZE, nullptr);
    if (!cycle->pool) 
	{
        memory_free(cycle);
        cycle = nullptr;
    }
    
    return cycle;
}

// conf 和error log init
int dn_cycle_init(cycle_t *cycle)
{
    log_t          *log = nullptr;
    conf_context_t *ctx = nullptr;
    conf_object_t  *conf_objects = nullptr;
    string_t        server = string_make("Server");
    
    if (cycle == nullptr) 
	{
        return NGX_ERROR;
    }
  
    cycle->conf_file.data = string_xxpdup(cycle->pool , &config_file);
    cycle->conf_file.len = config_file.len;
    
    if (!dfs_cycle) 
	{
        log = error_log_init_with_stderr(cycle->pool);
        if (!log) 
		{
            goto error;
        }
		
        cycle->error_log = log;
        cycle->pool->log = log;
        dfs_cycle = cycle;
    }
	
    error_log_set_handle(log, (log_time_ptr)time_logstr, nullptr);
 
    ctx = conf_context_create(cycle->pool);
    if (!ctx) 
	{
        goto error;
    }
    // 配置文件相关
    conf_objects = get_dn_conf_object();
    
    if (conf_context_init(ctx, &config_file, log, conf_objects) != NGX_OK)
	{
        goto error;
    }
    // parse conf file and make default
    if (conf_context_parse(ctx) != NGX_OK)
	{
        printf("configure parse failed at line %d\n", ctx->conf_line); 
		
        goto error;
    }
    
    cycle->sconf = conf_get_parsed_obj(ctx, &server);
    if (!cycle->sconf) 
	{
        printf("no Server conf\n");
		
        goto error;
    }

    // error log init
    // 这一块应该移动到 dfs module init
    dn_error_log_init(dfs_cycle);
	
    return NGX_OK;
    
error:
     return NGX_ERROR;
}

int dn_cycle_free(cycle_t *cycle)
{
    if (!cycle) 
	{
        return NGX_OK;
    }
   
    if (cycle->pool) 
	{
        pool_destroy(cycle->pool);
    }

    memory_free(cycle);

    return NGX_OK;
}

array_t * cycle_get_listen_for_cli()
{
    return &dfs_cycle->listening_for_cli;
}

