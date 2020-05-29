#include "dfs_notice.h"
#include "dfs_conn.h"

#define NOTICE_BUFF_SIZE 1024

static void noice_read_event_handler(event_t *ev);

// 进程间通信
// n -> tq_notice
// data -> task queue
// open channel and 分配 pfd
// 添加 event 事件
int notice_init(event_base_t *base, notice_t *n, 
	              wake_up_hander handler, void *data)
{
    conn_t  *c = nullptr;
    event_t *rev = nullptr;

    // pfd
    if (pipe_open(&n->channel) != NGX_OK)
	{
        return NGX_ERROR;
    }
    
    c = conn_get_from_mem(n->channel.pfd[0]); // 0是读端
    if (!c) 
	{
        goto error;
    }

    // 设置非阻塞
    conn_nonblocking(n->channel.pfd[0]);
    conn_nonblocking(n->channel.pfd[1]);
    n->call_back = handler; //do_paxos_task_handler // net_response_handler
    n->data = data; // task queue
    n->wake_up = notice_wake_up; // 向channel 中发送一个 "c"
    
    c->ev_base = base; //
    c->conn_data = n; // notice_t
    c->log = base->log;
    
    rev = c->read; //
    rev->handler = noice_read_event_handler; // 读事件发生后调用 n->call_back(n->data)

    // EPOLLIN
    if (epoll_add_event(base,rev, EVENT_READ_EVENT, 0) == NGX_ERROR)
	{
        dfs_log_error(base->log, DFS_LOG_FATAL, 0,"event adde failed");
		
        goto error;
    }
    
    n->log = base->log;

    return NGX_OK;
    
error:
    pipe_close(&n->channel);
    
    if (c) 
	{
        conn_free_mem(c);
    }
    
    return NGX_ERROR;
}

// 向channel 中发送一个 "c"
int notice_wake_up(notice_t *n)
{
    if (dfs_write_fd(n->channel.pfd[1], "C", 1) == NGX_ERROR) // 1 是写端
	{
        if (errno != DFS_EAGAIN) 
		{
            dfs_log_error(n->log, DFS_LOG_FATAL, 0, "notice wake up failed %d",
                errno);
        }
    }
    
    return NGX_OK;
}

// 读事件发生后调用 n->call_back(n->data)

static void noice_read_event_handler(event_t *ev)
{
    conn_t   *c = nullptr;
    notice_t *nt = nullptr;
    uchar_t   buff[NOTICE_BUFF_SIZE];
    int       n = 0;
    char     *errmsg = nullptr;
    
    c = (conn_t *)ev->data;
    nt = (notice_t *)c->conn_data;
    
    while (1) 
	{
        // read "c"
        n = dfs_read_fd(nt->channel.pfd[0], buff, NOTICE_BUFF_SIZE);
        if (n > 0 || (n < 0 && errno == DFS_EINTR)) 
		{
            continue;
        }

        if (n == 0) 
		{
            errmsg = (char *)"read 0 byte!";
        } 
		else if (errno != DFS_EAGAIN) 
		{
            errmsg = (char *)"notice read failed";
        }
        
        break;
    }

    if (errmsg) 
	{
        dfs_log_error(nt->log, DFS_LOG_FATAL, 0,
                "pipe fd[%d] %s", nt->channel.pfd[0], errmsg);
    }
	
    nt->call_back(nt->data);
}

