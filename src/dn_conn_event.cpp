#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dfs_time.h"
#include "dfs_memory.h"
#include "dfs_event.h"
#include "dfs_conn_pool.h"
#include "dfs_conn_listen.h"
#include "dn_conf.h"
#include "dn_time.h"
#include "dn_process.h"
#include "nn_request.h"
#include "dn_thread.h"

#define CONF_SERVER_UNLIMITED_ACCEPT_N 0
#define ADDR_MAX_LEN                   16

static void listen_rev_handler(event_t *ev);

// 初始化listening 并 open_listening
// listen_rev_handler 处理 listening 事件
int conn_listening_init(cycle_t *cycle)
{
    listening_t   *ls = nullptr;
    conf_server_t *sconf = nullptr;
    uint32_t       i = 0;
    server_bind_t *bind_for_cli = nullptr;
    
    sconf = (conf_server_t *)dfs_cycle->sconf;
	bind_for_cli = (server_bind_t *)sconf->bind_for_cli.elts; //cli server_bind_t addr prot

	cycle->listening_for_cli.elts = pool_calloc(cycle->pool, 
		sizeof(listening_t) * sconf->bind_for_cli.nelts);
    if (!cycle->listening_for_cli.elts) 
	{
         dfs_log_error(cycle->error_log, DFS_LOG_FATAL, 0,
            "no space to alloc listening_for_cli pool");
		 
        return NGX_ERROR;
    }

	cycle->listening_for_cli.nelts = 0;
    cycle->listening_for_cli.size = sizeof(listening_t);
    cycle->listening_for_cli.nalloc = sconf->bind_for_cli.nelts;
    cycle->listening_for_cli.pool = cycle->pool;

	for (i = 0; i < sconf->bind_for_cli.nelts; i++) 
	{
	    // add listening to array
	    // init listening
        ls = conn_listening_add(&cycle->listening_for_cli, cycle->pool,
            cycle->error_log, inet_addr((char *)bind_for_cli[i].addr.data), 
            bind_for_cli[i].port, listen_rev_handler, 
            ((conf_server_t*)cycle->sconf)->recv_buff_len, 
            ((conf_server_t*)cycle->sconf)->recv_buff_len);
		
        if (!ls) 
		{
            return NGX_ERROR;
        }

		strcpy(cycle->listening_ip, (const char *)bind_for_cli[i].addr.data);

    }

	// open listening
	// listening fd = sockfd
	if (conn_listening_open(&cycle->listening_for_cli, cycle->error_log) 
		!= NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

// 处理函数
// accept handler
static void listen_rev_handler(event_t *ev)
{
    int           s;
    char          sa[DFS_SOCKLEN];
    log_t        *log = nullptr;
    uchar_t      *address = nullptr;
    conn_t       *lc = nullptr;
    conn_t       *nc = nullptr;
    event_t      *wev = nullptr;
    socklen_t     socklen;
    listening_t  *ls = nullptr;
    int           i = 0;
    conn_pool_t  *conn_pool = nullptr;

    // 第一次处理为1？监听过后事件失效
    ev->ready = 0;
    lc = (conn_t *)ev->data;
    ls = lc->listening;
    
    errno = 0;
    socklen = DFS_SOCKLEN;
    conn_pool = thread_get_conn_pool();
    
    for (i = 0; //
        ev->available == CONF_SERVER_UNLIMITED_ACCEPT_N || i < ev->available;
        i++) 
    {
        /*accept一个新的连接, accept 的时候 lc->fd = ls->fd*/
        s = accept(lc->fd, (struct sockaddr *) sa, &socklen);

        if (s == NGX_INVALID_FILE)
		{
            if (errno == DFS_EAGAIN) 
			{
                dfs_log_debug(dfs_cycle->error_log, DFS_LOG_DEBUG, errno,
                    "conn_accept: accept return EAGAIN");
            } 
			else if (errno == DFS_ECONNABORTED) 
			{
                dfs_log_debug(dfs_cycle->error_log, DFS_LOG_DEBUG, errno,
                    "conn_accept: accept client aborted connect");
            } 
			else 
			{
                dfs_log_error(dfs_cycle->error_log, DFS_LOG_WARN, errno,
                    "conn_accept: accept failed");
            }
			
            dfs_log_debug(dfs_cycle->error_log, DFS_LOG_DEBUG, 0,
                "conn_accept");
			
            return;
        }
        /*从connections数组中获取一个connecttion slot来维护新的连接*/
        nc = conn_pool_get_connection(conn_pool);
        if (!nc) 
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, 0,
                "conn_accept: get connection failed");
			
            close(s);
			
            return;
        }
		// set conn fd = s
        conn_set_default(nc, s);
       
        if (!nc->pool)
		{
            nc->pool = pool_create(ls->conn_psize, DEFAULT_PAGESIZE, 
				dfs_cycle->error_log);
            if (!nc->pool) 
			{
                dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, 0,
                    "conn_accept: create connection pool failed");
				
                goto error;
            }
        }
		
        nc->sockaddr = (struct sockaddr *)pool_alloc(nc->pool, socklen);
        if (!nc->sockaddr) 
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, 0,
                "conn_accept: pool alloc sockaddr failed");
			
            goto error;
        }
		
        memory_memcpy(nc->sockaddr, sa, socklen);
		
        if (conn_nonblocking(s) == NGX_ERROR)
		{
             dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
                "conn_accept: setnonblocking failed");
			 
             goto error;
        }
		
        log = dfs_cycle->error_log;
        /*初始化新连接*/
        nc->recv = dfs_recv; // in dfs_sys_io.c
        nc->send = dfs_send;
        nc->recv_chain = dfs_recv_chain;
        nc->send_chain = dfs_send_chain;
        nc->sendfile_chain = dfs_sendfile_chain;
        nc->log = log;
        nc->listening = ls;
        nc->socklen = socklen;
        wev = nc->write;
        wev->ready = NGX_FALSE;

        nc->addr_text.data = (uchar_t *)pool_calloc(nc->pool, ADDR_MAX_LEN);
        if (!nc->addr_text.data) 
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
                "conn_accept: pool alloc addr_text failed");
			
            goto error;
        }
		
        address = (uchar_t *)inet_ntoa(((struct sockaddr_in *)
            nc->sockaddr)->sin_addr);
        if (address) 
		{
            nc->addr_text.len = string_strlen(address);
        }
		
        if (nc->addr_text.len == 0) 
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
                "conn_accept: inet_ntoa server address failed");
			
            goto error;
        }
		
        memory_memcpy(nc->addr_text.data, address, nc->addr_text.len);

        dfs_log_debug(dfs_cycle->error_log, DFS_LOG_DEBUG, 0,
            "conn_accept: fd:%d conn:%p addr:%V, port:%d ls:%V",
            s, nc, &nc->addr_text,
            ntohs(((struct sockaddr_in *)nc->sockaddr)->sin_port), 
            &ls->addr_text);
		
        nc->accept_time = *time_timeofday();
        //
        nn_conn_init(nc);
    }
		
error:
    conn_close(nc);
    conn_pool_free_connection(conn_pool, nc);
}

