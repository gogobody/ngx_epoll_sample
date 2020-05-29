#include "nn_net_response_handler.h"

#define task_data(q, type, link) \
    (type *) ((uchar_t *) q - offsetof(type, link))

static void write_back_pack_task(task_queue_node_t *node);

// 重新push到不同的队列里 // dn 和 cli push 到bque队列，其他线程push到 tq队列里
// push task thread
// notice_wake_up
int write_back(task_queue_node_t *node)
{
    task_t       *task = nullptr;
    nn_wb_t      *wbt = nullptr; // write back
    pthread_t     id;
    dfs_thread_t *th = nullptr;

    task = &node->tk;
    wbt  = (nn_wb_t *)task->opq;
	/*
	*struct nn_wb_s 
{
    nn_conn_t    *mc;
    dfs_thread_t *thread;
};
	*/
	
    queue_init(&node->qe);

    if ( THREAD_CLI_TEST == wbt->thread->type)
	{
        th = get_local_thread();
        id = th->thread_id;
        id %= wbt->thread->queue_size; // 做个简单的 hash

        push_task(&wbt->thread->bque[id], node); // 交给线程数组中另外的thread
    }
	else //
	{
        push_task(&wbt->thread->tq, node);
    }
    
    return notice_wake_up(&wbt->thread->tq_notice);
}

// n->call_back
// 对数组的bque 和 tq全部执行 write_back_pack_queue
// do task
void net_response_handler(void *data)
{
    queue_t       q;
    task_queue_t *tq = nullptr;
    dfs_thread_t *th = nullptr;
    int           i = 0;   
    
    th = (dfs_thread_t *)data; // 区别cli thread和 dn thread

    // 这里应该都是 THREAD_DN 或者 THREAD_CLI_TEST
    if (THREAD_CLI_TEST == th->type)
	{
        for (i = 0; i < th->queue_size; i++) 
        {
            tq = &th->bque[i];
			
            queue_init(&q);
            pop_all(tq, &q);
            //
            write_back_pack_queue(&q, NGX_TRUE);
        }
    } else
	{
        tq = &th->tq; //task queue // write back时会写入 tq

        printf("im not thread dn or thread cli, %d",th->type);

        queue_init(&q);
        pop_all(tq, &q);
        write_back_pack_queue(&q, NGX_TRUE);
    }
}

void write_back_notice_call(void *data)
{
    queue_t       q;
    task_queue_t *tq = nullptr;

    tq = (task_queue_t *)data;

    queue_init(&q);
    pop_all(tq, &q);
    write_back_pack_queue(&q, NGX_TRUE);
}

// 插入 task -> mc conn -> out task
// mc->write_event_handler = nn_conn_write_handler;
// send out buffer
// add event
static void write_back_pack_task(task_queue_node_t *node)
{
    task_t  *task = nullptr;
    nn_wb_t *wbt = nullptr;

    task = &node->tk;
    wbt  = (nn_wb_t *)task->opq;
	
    nn_conn_outtask(wbt->mc, task);
}

// write tasks back which in q, send is status code
// 插入 task -> mc conn -> out task
// mc->write_event_handler = nn_conn_write_handler;
// send out buffer
// add event
void write_back_pack_queue(queue_t *q, int send) // send = 1
{
    queue_t           *qn = nullptr;
    task_queue_node_t *node = nullptr;
    
    while (!queue_empty(q)) 
	{
        qn = queue_head(q);
        queue_remove(qn);
        node = queue_data(qn, task_queue_node_t, qe);

        write_back_pack_task(node);
    }
}

int write_back_task(task_t* t)
{
    task_queue_node_t *node = nullptr;
    node = queue_data(t, task_queue_node_t, tk);
	
    return write_back(node);
}

int trans_task(task_t *task, dfs_thread_t *thread)
{
    task_queue_node_t *node = nullptr;

    node  = task_data(task, task_queue_node_t, tk);
     
    push_task(&thread->tq, node);
    
    return notice_wake_up(&thread->tq_notice);
}

