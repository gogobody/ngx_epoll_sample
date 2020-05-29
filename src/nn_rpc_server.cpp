#include "nn_rpc_server.h"
#include "dn_thread.h"
#include "task_test.h"

int nn_rpc_worker_init(cycle_t *cycle)
{
    //conf_server_t *conf = nullptr;
	//conf = (conf_server_t *)cycle->sconf;

    return NGX_OK;
}

int nn_rpc_worker_release(cycle_t *cycle)
{
    (void) cycle;

    return NGX_OK;
}

// first do task
// 重新push到不同的队列里 // dn 和 cli push 到bque队列，其他线程push到 tq队列里
// from notice_wake_up
int nn_rpc_service_run(task_t *task)
{
    int optype = task->cmd;
	
	switch (optype)
    {
    case TASK_TEST: // this is a example
        task_test(task);
		break;

		
	default:
		dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, 0, 
			"unknown optype: ", optype);
		
		return NGX_ERROR;
	}
	
    return NGX_OK;
}

