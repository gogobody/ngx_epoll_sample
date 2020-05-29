
#include <stdlib.h>

#include "nn_task_handler.h"
#include "nn_task_queue.h"
#include "dn_thread.h"
#include "nn_rpc_server.h"

// nn_rpc_service_run
static void do_task(task_t *task)
{
    assert(task);
	nn_rpc_service_run(task);
}

// do task
// 对应Thread_TASK 的task queue
// 分发 task 到不同线程
void do_task_handler(void *q) // thread->tq
{
	task_queue_node_t *tnode = nullptr;
	task_t            *t = nullptr;
	queue_t           *cur = nullptr;
	queue_t            qhead;
	task_queue_t      *tq = nullptr;
    dfs_thread_t      *thread = nullptr;

	tq = (task_queue_t *)q;
    thread = get_local_thread(); // task_threads[]
	
    queue_init(&qhead);
	pop_all(tq, &qhead);
	
	cur = queue_head(&qhead);
	
	while (!queue_empty(&qhead) && thread->running)
	{
		// |task_queue_node_t    |  qe |
		// |                     |     |
		// 所以 qe 减去偏移 得到 对应 task_queue_node_t
		tnode = queue_data(cur, task_queue_node_t, qe);
		t = &tnode->tk;
		
        queue_remove(cur);

        do_task(t);
		
		cur = queue_head(&qhead);
	}
}

