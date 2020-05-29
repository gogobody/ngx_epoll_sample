#include <netinet/in.h>

#include "dfs_conn.h"
#include "dfs_memory.h"
#include "dfs_event_timer.h"

int conn_connect_peer(conn_peer_t *pc, event_base_t *ep_base)
{
    int      rc = 0;
    conn_t  *c = nullptr;
    event_t *wev = nullptr;

    if (!pc) 
	{
        return NGX_ERROR;
    }

    c = pc->connection;
    if (!c) 
	{
        return NGX_BUSY;
    }
    
    if (c->fd != NGX_INVALID_FILE)
	{
        goto connecting;
    }
    
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd == NGX_ERROR)
	{
        return NGX_ERROR;
    }
    
    conn_nonblocking(c->fd);
   
    c->recv = dfs_recv;
    c->send = dfs_send;
    c->recv_chain = dfs_recv_chain;
    c->send_chain = dfs_send_chain;
    c->sendfile_chain = dfs_sendfile_chain;
    c->sendfile = NGX_TRUE;
	
    if (pc->sockaddr->sa_family != AF_INET) 
	{
        c->tcp_nopush = CONN_TCP_NOPUSH_DISABLED;
        c->tcp_nodelay = CONN_TCP_NODELAY_DISABLED;
    }
	
    c->tcp_nodelay = CONN_TCP_NODELAY_UNSET;
    c->tcp_nopush = CONN_TCP_NOPUSH_UNSET;

    wev = c->write;
    
connecting:
    
    // add conn to epoll, read write
    if (event_add_conn(ep_base, c) == NGX_ERROR)
	{
        return NGX_ERROR;
    }
    
    errno = 0;
    rc = connect(c->fd, pc->sockaddr, pc->socklen);
    if (rc == NGX_ERROR)
	{
        if (errno == DFS_EINPROGRESS) 
		{
            return NGX_AGAIN;
        }
		
        return NGX_ERROR;
    }

    wev->ready = 1;

    return NGX_OK;
}

int conn_tcp_nopush(int s)
{
    int cork = 1;
	
    return setsockopt(s, IPPROTO_TCP, TCP_CORK,
        (const void *) &cork, sizeof(int));
}

int conn_tcp_push(int s)
{
    int cork = 0;
	
    return setsockopt(s, IPPROTO_TCP, TCP_CORK,
        (const void *) &cork, sizeof(int));
}

int conn_tcp_nodelay(int s)
{
    int nodelay = 1;
	
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
        (const void *) &nodelay, sizeof(int));
}

int conn_tcp_delay(int s)
{
    int nodelay = 0;
	
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
        (const void *) &nodelay, sizeof(int));
}

// 根据 fd 初始化 connection
conn_t * conn_get_from_mem(int s)
{
    conn_t  *c   = nullptr;
    event_t *rev = nullptr;
    event_t *wev = nullptr;

    c   = (conn_t *)memory_calloc(sizeof(conn_t));
    rev = (event_t *)memory_calloc(sizeof(event_t));
    wev = (event_t *)memory_calloc(sizeof(event_t));

    if (!c || !rev || !wev) 
	{
        return nullptr;
    }
	
    c->read = rev;
    c->write = wev;
    // 根据 eventfd 初始化 connection
    conn_set_default(c, s);
	
    return c;
}

// 根据 eventfd 设置conn
void conn_set_default(conn_t *c, int s)
{
    event_t  *rev = nullptr;
    event_t  *wev = nullptr;
    uint32_t  instance;
    uint32_t  last_instance;

    c->fd = s; // set conn fd

    rev = c->read; // read event
    wev = c->write; // write event
    instance = rev->instance;
    last_instance = rev->last_instance;
    c->sent = 0;

    c->conn_data = nullptr;
    c->next = nullptr;
    c->error = 0;
    c->listening = nullptr;
    c->next = nullptr;
    c->sendfile = NGX_FALSE;
    c->sndlowat = 0;
    c->sockaddr = nullptr;
    memory_zero(&c->addr_text, sizeof(string_t));
    c->socklen = 0;
    //set rev & wev->instance to !last->instance
    memory_zero(rev, sizeof(event_t));
    memory_zero(wev, sizeof(event_t));
    rev->instance = !instance;
    wev->instance = !instance;
    rev->last_instance = last_instance;
    
    rev->data = c;
    wev->data = c;
    wev->write = NGX_TRUE;
}

void conn_close(conn_t *c)
{
    if (!c) 
	{
        return;
    }
	
    if (c->fd > 0) 
	{
        close(c->fd);
        c->fd = NGX_INVALID_FILE;
    
        // remove timers
        if (c->read->timer_set && c->ev_timer) 
		{
            event_timer_del(c->ev_timer, c->read);
        }

        if (c->write->timer_set && c->ev_timer) 
		{
            event_timer_del(c->ev_timer, c->write);
        }

        if (c->ev_base) 
		{
            event_del_conn(c->ev_base, c, EVENT_CLOSE_EVENT);
        }
    } 
}

void conn_release(conn_t *c)
{
    conn_close(c);

    if (c->pool) 
	{
        pool_destroy(c->pool);
    }
	
    c->pool = nullptr;
}

void conn_free_mem(conn_t *c)
{
    event_t *rev = nullptr;
    event_t *wev = nullptr;

    rev = c->read;
    wev = c->write;
    memory_free(rev);
    memory_free(wev);
    memory_free(c);
}

