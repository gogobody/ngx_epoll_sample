#include <sys/socket.h>
#include <net/if_arp.h>

#include "dfs_epoll.h"
#include "dfs_event_timer.h"
#include "dn_thread.h"
#include "dn_worker_process.h"
#include "nn_net_response_handler.h"
#include "dn_conf.h"

#define CONN_TIME_OUT        300000
#define TASK_TIME_OUT        100

#define NN_TASK_POOL_MAX_SIZE 64
#define NN_TASK_POOL_MIN_SIZE 8

extern dfs_thread_t  *cli_thread;

static void nn_event_process_handler(event_t *ev);
static void nn_conn_read_handler(nn_conn_t *mc);
static void nn_conn_write_handler(nn_conn_t *mc);
static int nn_conn_out_buffer(conn_t *c, buffer_t *b);
static void nn_conn_free_queue(nn_conn_t *mc);
static void nn_conn_close(nn_conn_t *mc);
static int  nn_conn_recv(nn_conn_t *mc);
static int  nn_conn_decode(nn_conn_t *mc);


static void  nn_conn_timer_handler(event_t *ev)
{
    nn_conn_t *mc = (nn_conn_t *)ev->data;
	
    event_timer_del(mc->connection->ev_timer, ev);
    if (event_handle_read(mc->connection->ev_base, mc->connection->read, 0)) 
    {
        nn_conn_finalize(mc);
		
        return;
    }

    nn_conn_write_handler(mc);
    nn_conn_read_handler(mc);
}

// 入口。。。
// from listen_rev_handler
// 初始化分配 max task个 wb_node *

void nn_conn_init(conn_t *c)
{
    event_t      *rev = nullptr;
    event_t      *wev = nullptr;
    nn_conn_t    *mc = nullptr;
    pool_t       *pool = nullptr;
    wb_node_t    *node = nullptr;
    wb_node_t    *buff = nullptr;
	dfs_thread_t *thread = nullptr;
    int32_t       i = 0;
   
    thread = get_local_thread();
	
    pool = pool_create(4096, 4096, dfs_cycle->error_log);
    if (!pool) 
	{
        dfs_log_error(dfs_cycle->error_log,
             DFS_LOG_FATAL, 0, "pool create failed");
		
        goto error;
    }
	
    rev = c->read;
    wev = c->write;
    
    if (!c->conn_data) 
	{
        c->conn_data = pool_calloc(pool, sizeof(nn_conn_t));
    }
	// mc => nn_conn
    mc = (nn_conn_t *)c->conn_data;
    if (!mc) 
	{
        dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, 0, 
			"mc create failed");
		
        goto error;
    }
	
    mc->mempool = pool;
    mc->log = dfs_cycle->error_log;
    mc->in  = buffer_create(mc->mempool, 
   			 ((conf_server_t*)dfs_cycle->sconf)->recv_buff_len * 2);
    mc->out = buffer_create(mc->mempool,
			((conf_server_t*)dfs_cycle->sconf)->send_buff_len * 2);
    if (!mc->in || !mc->out) 
	{
        dfs_log_error(mc->log, DFS_LOG_ALERT, 0, "buffer_create failed");
		
        goto error;
    }
	
    c->ev_base = &thread->event_base;
    c->ev_timer = &thread->event_timer;
	mc->max_task = ((conf_server_t*)dfs_cycle->sconf)->max_tqueue_len;
    mc->count = 0;
    mc->connection = c;
    mc->slow = 0;
	
    snprintf(mc->ipaddr, sizeof(mc->ipaddr), "%s", c->addr_text.data);
	
    queue_init(&mc->free_task);

    // 分配 max_task 的内存
    buff = (wb_node_t *)pool_alloc(mc->mempool, mc->max_task * sizeof(wb_node_t));
    if (!buff) 
	{
        goto error;
    }

    // 初始化分配 max task个 wb_node *
    for (i = 0; i < mc->max_task; i++) 
	{
        node = buff + i; // 每个node都是一个单独的 queue
        node->qnode.tk.opq = &node->wbt;
		node->qnode.tk.data = nullptr;
        (node->wbt).mc = mc;

        // process event 时 accept事件 只会由 THREAD_DN OR THREAD_CLI_TEST 处理
        // todo: change type here
		if (THREAD_CLI_TEST == thread->type)
		{
			(node->wbt).thread = cli_thread;
		}
//		if (THREAD_DN == thread->type)
//        {
//            (node->wbt).thread = dn_thread;
//        }


        queue_insert_head(&mc->free_task, &node->qnode.qe);
    }
	
    memset(&mc->ev_timer, 0, sizeof(event_t));
    mc->ev_timer.data = mc; //
    mc->ev_timer.handler = nn_conn_timer_handler;

    rev->handler = nn_event_process_handler;
    wev->handler = nn_event_process_handler;
	
    queue_init(&mc->out_task);
    // recv buf to mc->in
    // decode task from mc->in
    // dispatch task to task_threads[]
    // called in nn_event_process_handler
    mc->read_event_handler = nn_conn_read_handler;
	
    nn_conn_update_state(mc, ST_CONNCECTED);

	// add read event
    if (event_handle_read(c->ev_base, rev, 0) == NGX_ERROR)
	{
        dfs_log_error(mc->log, DFS_LOG_ALERT, 0, "add read event failed");
		
        goto error;
    }

    return;
	
error:
    if (mc && mc->mempool) 
	{
        pool_destroy(mc->mempool);
    }
    
    conn_release(c);
    conn_pool_free_connection(&thread->conn_pool, c);
}

// read \ write event handler
// 执行对应mc 的 write event handler 或者 read event handler
static void nn_event_process_handler(event_t *ev)
{
    conn_t    *c = nullptr;
    nn_conn_t *mc = nullptr;
	
    c = (conn_t *)ev->data;
    mc = (nn_conn_t *)c->conn_data;

    if (ev->write) 
	{
        if (!mc->write_event_handler) 
		{
            dfs_log_error(mc->log, DFS_LOG_ERROR,
                0, "write handler nullptr, fd:%d", c->fd);

            return;
        }

        mc->write_event_handler(mc);
    } 
	else 
	{
        if (!mc->read_event_handler) 
		{
            dfs_log_debug(mc->log, DFS_LOG_DEBUG,
                0, "read handler nullptr, fd:%d", c->fd);

            return;
        }
		
        mc->read_event_handler(mc); //nn_conn_read_handler
    }
}

//static void nn_empty_handler(event_t *ev)
//{   
//}


// recv buf to mc->in
static int nn_conn_recv(nn_conn_t *mc)
{
    int     n = 0;
    conn_t *c = nullptr;
    size_t  blen = 0;
	
	c = mc->connection;
	
    while (1) 
	{
		buffer_shrink(mc->in);
		
    	blen = buffer_free_size(mc->in);
	    if (!blen) 
		{
	        return NGX_BUFFER_FULL;
	    }
		
   		n = c->recv(c, mc->in->last, blen); //sysio_unix_recv
		if (n > 0) // move point to buffer end
		{
			mc->in->last += n;
			
			continue;
		}
		
	    if (n == 0) 
		{
	        return NGX_CONN_CLOSED;
	    }
		
	    if (n == NGX_ERROR)
		{
	        return NGX_ERROR;
	    }
		
	    if (n == NGX_AGAIN)
		{
			return NGX_AGAIN;
	    }
	}
	
	return NGX_OK;
}

// from nn_conn_read_handler
// decode task from mc->in
// dispatch task to task_threads[]
static int nn_conn_decode(nn_conn_t *mc)
{
    int                rc = 0;
    task_queue_node_t *node = nullptr;
	
	while (true)
	{
        // pop free task from free_task queue
        node = (task_queue_node_t *)nn_conn_get_task(mc);
		if (!node) 
		{
            dfs_log_error(mc->log,DFS_LOG_ERROR, 0, 
				"mc to repiad remove from epoll");
   
			mc->slow = 1;
			event_del_read(mc->connection->ev_base, mc->connection->read);
            event_timer_add(mc->connection->ev_timer, &mc->ev_timer, 1000);
			
            return NGX_BUSY;
    	}	

		// decode task to _in buffer
		printf("recv: %s %d\n",(char*)mc->in->pos, buffer_size(mc->in));

        rc = task_decode(mc->in, &node->tk);
        if (rc == NGX_OK)
		{
            // dispatch task when recv it
            // push task to dispatch_last_task->tq
            // notice_wake_up (&dispatch_last_task->tq_notice
            dispatch_task(node);

            continue;
        }
		
        if (rc == NGX_ERROR)
		{
			nn_conn_free_task(mc, &node->qe);
			buffer_reset(mc->in);
			
            return NGX_ERROR;
        }
            
        if (rc == NGX_AGAIN)  // buffer 不够了也
		{
            nn_conn_free_task(mc, &node->qe);
			buffer_shrink(mc->in);
			
            return NGX_AGAIN;
        }
	}
   	
	return NGX_OK;
}

// 入口。。。
// listen_rev_handler
// nn_conn_init
// recv buf to mc->in
// decode task from mc->in
// dispatch task to task_threads[]
static void nn_conn_read_handler(nn_conn_t *mc)
{
	int   rc = 0;
	char *err = nullptr;
	
    if (mc->state == ST_DISCONNCECTED) 
	{
        return;
    }

    // recv buf to mc->in
    rc = nn_conn_recv(mc);
    uchar_t  * addr = mc->connection->addr_text.data;
//    printf("%s\n",addr);
    char tmpchar[50];
    switch (rc) 
	{
    case NGX_CONN_CLOSED:
//        err = (char *)"conn closed";
		sprintf(tmpchar,"%s conn closed",addr);
		err = tmpchar;
        goto error;
			
    case NGX_ERROR:
        err = (char *)"recv error, conn to be close";
		
        goto error;
			
    case NGX_AGAIN:
    case NGX_BUFFER_FULL:
        break;
    default:
        goto error;
    }
    // decode task from mc->in
    // dispatch task to task_threads[]


    rc = nn_conn_decode(mc);
    if (rc == NGX_ERROR)
	{
    	err = (char *)"proto error";
	
    	goto error;
    }
	
   	return;
	
error:

    dfs_log_error(mc->log, DFS_LOG_ALERT, 0, err);
    nn_conn_finalize(mc);
}

// mc connect write event
static void nn_conn_write_handler(nn_conn_t *mc)
{
    conn_t *c = nullptr;
	
    if (mc->state == ST_DISCONNCECTED) 
	{
        return;
    }
	
    c = mc->connection; //获取到被动连接的connection
    
    if (c->write->timedout) 
	{
        dfs_log_error(mc->log, DFS_LOG_FATAL, 0,
                "conn_write timer out");
		
        nn_conn_finalize(mc);
		
        return;
    }
   
    if (c->write->timer_set) 
	{
        event_timer_del(c->ev_timer, c->write);
    }
    // mc->write_event_handler = nn_conn_write_handler;
    // send buffer
    // add event
	nn_conn_output(mc);  
}

void nn_conn_close(nn_conn_t *mc)
{
    conn_pool_t *pool = nullptr;

    pool = thread_get_conn_pool();
    if (mc->connection) 
	{
        mc->connection->conn_data = nullptr;
        conn_release(mc->connection);
        conn_pool_free_connection(pool, mc->connection);
        mc->connection = nullptr;
    }
}

int nn_conn_is_connected(nn_conn_t *mc)
{
    return mc->state == ST_CONNCECTED;
}

// send buffer
static int nn_conn_out_buffer(conn_t *c, buffer_t *b)
{
    size_t size = 0;
    int    rc = 0;
    
    if (!c->write->ready) 
	{
        return NGX_AGAIN;
    }
  
    size = buffer_size(b);
	
    while (size) 
	{      
        rc = c->send(c, b->pos, size);
        if (rc < 0) 
		{
            return rc;
        }
        
        b->pos += rc,
        size -= rc;
    }
	
	buffer_reset(b);
    
    return NGX_OK;
}

// 插入 task -> mc conn -> out task
// mc->write_event_handler = nn_conn_write_handler;
// send out buffer
// add event
int nn_conn_outtask(nn_conn_t *mc, task_t *t)
{   
    task_queue_node_t *node =nullptr;
	node = queue_data(t, task_queue_node_t, tk);   
	
	if (mc->state != ST_CONNCECTED) 
	{
		nn_conn_free_task(mc, &node->qe);
        
		return NGX_OK;
	}

	queue_insert_tail(&mc->out_task, &node->qe);
    
	return nn_conn_output(mc);
}

// mc->write_event_handler = nn_conn_write_handler;
// send out buffer
// add event
int nn_conn_output(nn_conn_t *mc)
{
    conn_t            *c = nullptr;
    int                rc =0;
	task_t            *t = nullptr;
	task_queue_node_t *node = nullptr;
	queue_t           *qe = nullptr;
	char              *err_msg = nullptr;
    
    c = mc->connection;
    
    if (!c->write->ready && buffer_size(mc->out) > 0 ) 
	{
        return NGX_AGAIN;
    }
    
    mc->write_event_handler = nn_conn_write_handler;
    
repack:
	buffer_shrink(mc->out);
	
	while (!queue_empty(&mc->out_task)) 
	{
    	qe = queue_head(&mc->out_task);
		node = queue_data(qe, task_queue_node_t, qe);
		t =&node->tk;

		// encode task to out buffer
		rc = task_encode(t, mc->out);
		if (rc == NGX_OK)  // 这个task push完成就释放空间
		{
			queue_remove(qe);
			nn_conn_free_task(mc, qe);
			
			continue;
		}

		if (rc == NGX_AGAIN)  // 塞满一个buffer 就发
		{
			goto send;
		}
		
		if (rc == NGX_ERROR)
		{
            queue_remove(qe);
			nn_conn_free_task(mc, qe);
        }
	}

send:
    if(!buffer_size(mc->out))  // buffer size 0
	{
        return NGX_OK;
    }

    // send buffer
	rc = nn_conn_out_buffer(c, mc->out);
    if (rc == NGX_ERROR)
	{
        err_msg = (char *)"send data error  close conn";
		
		goto close;
    }
    
    if (rc == NGX_AGAIN)
	{
        if (event_handle_write(c->ev_base, c->write, 0) == NGX_ERROR)
		{
            dfs_log_error(mc->log, DFS_LOG_FATAL, 0, "event_handle_write");
			
        	return NGX_ERROR;
        }
		
        event_timer_add(c->ev_timer, c->write, CONN_TIME_OUT);
		
        return NGX_AGAIN;
    }
    
    goto repack;
    
close:
	dfs_log_error(c->log, DFS_LOG_FATAL, 0, err_msg);
    nn_conn_finalize(mc);
	
	return NGX_ERROR;
}

// pop free task from free_task queue
void * nn_conn_get_task(nn_conn_t *mc)
{
    queue_t           *queue = nullptr;
    task_queue_node_t *node = nullptr;
	
	if (mc->count >= mc->max_task)
	{
		dfs_log_error(mc->log,DFS_LOG_DEBUG, 0, 
			"get mc taskcount:%d", mc->count);
		
		return nullptr;
	}
	
    queue = queue_head(&mc->free_task); //
    node = queue_data(queue, task_queue_node_t, qe); //
    queue_remove(queue);
    mc->count++; //
	
    return node;  
}


void nn_conn_free_task(nn_conn_t *mc, queue_t *q)
{
   mc->count--;

   task_queue_node_t *node = queue_data(q, task_queue_node_t, qe);
   task_t *task = &node->tk;
   if (nullptr != task->data && task->data_len > 0)
   {
       free(task->data);
	   task->data = nullptr;
   }
   
   queue_init(q);
   queue_insert_tail(&mc->free_task, q);
   
   if (mc->state == ST_DISCONNCECTED && mc->count == 0) 
   {
       nn_conn_finalize(mc);
   }
}

static void nn_conn_free_queue(nn_conn_t *mc)
{
    queue_t *qn = nullptr;
	
    while (!queue_empty(&mc->out_task)) 
	{
        qn = queue_head(&mc->out_task);
        queue_remove(qn);
        nn_conn_free_task(mc, qn);
    }
}

int nn_conn_update_state(nn_conn_t *mc, int state)
{
    mc->state = state;
	
    return NGX_OK;
}

int nn_conn_get_state(nn_conn_t *mc)
{
    return mc->state;
}

void nn_conn_finalize(nn_conn_t *mc)
{
    if (mc->state == ST_CONNCECTED) 
	{
        if (mc->ev_timer.timer_set) 
		{
            event_timer_del(mc->connection->ev_timer,&mc->ev_timer);
        }
		
        nn_conn_close(mc);
        nn_conn_free_queue(mc);
        nn_conn_update_state(mc, ST_DISCONNCECTED);
    }

    if (mc->count> 0) 
	{
        return;
    }
	
    pool_destroy(mc->mempool);
}

