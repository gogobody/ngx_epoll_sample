#include "dfs_event.h"
#include "dfs_conn.h"

// process posted events
// process connection->event
void event_process_posted(volatile queue_t *posted, log_t *log)
{
    int      i = 0;
    conn_t  *c;
    event_t *ev = nullptr;
    queue_t *eq = nullptr;
	
    while (!queue_empty(posted)) 
	{
        eq = queue_head(posted);
		
        dfs_log_debug(log, DFS_LOG_DEBUG, 0,
            "event_process_posted: posted %l, eq %l eq->prev %l "
            "eq->next %l index %d", (size_t)posted, (size_t)eq,
            (size_t)eq->prev, (size_t)eq->next, i);
		
        queue_remove(eq);
		//
        ev = queue_data(eq, event_t, post_queue);
        if (!ev) 
		{
            dfs_log_debug(log, DFS_LOG_DEBUG, 0,
                "event_process_posted: ev is nullptr");
			
            return;
        }

        c = (conn_t *)ev->data;
		
        dfs_log_debug(log, DFS_LOG_DEBUG, 0,
            "event_process_posted: fd:%d conn:%p, timer key:%M write:%d",
            c->fd, c, ev->timer.key, ev->write);

        if (c->fd != NGX_INVALID_FILE)
		{
            dfs_log_debug(log, DFS_LOG_DEBUG, 0,
                "%s: fd:%d conn:%p, timer key:%M write:%d, handle %p", __func__,
                c->fd, c, ev->timer.key, ev->write, ev->handler);

            ev->handler(ev); //listen_rev_handler in dn_conn_event.c

        }
		else 
		{
            dfs_log_debug(log, DFS_LOG_DEBUG, 0,
                "event_process_posted: stale event conn:%p, fd:%d", c, c->fd);
        }
		
        i++;
    }
}

int event_handle_read(event_base_t *base, event_t *rev, uint32_t flags)
{
    // 第一次 rev->active ready =0
    if (!rev->active && !rev->ready) 
	{
        if (epoll_add_event(base, rev, EVENT_READ_EVENT,
            EVENT_CLEAR_EVENT) == NGX_ERROR)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

int event_del_read(event_base_t *base, event_t *rev)
{
    rev->ready = NGX_FALSE;
	
    return event_delete(base, rev, EVENT_READ_EVENT, EVENT_CLEAR_EVENT);
}

int event_handle_write(event_base_t *base, event_t *wev, size_t lowat)
{
    if (!wev->active && !wev->ready) 
	{
        if (epoll_add_event(base, wev, EVENT_WRITE_EVENT,
            EVENT_CLEAR_EVENT) == NGX_ERROR)
        {
            return NGX_ERROR;
        }
    } 
	else 
	{
        dfs_log_debug(base->log,DFS_LOG_DEBUG, 0,
            "%s: fd:%d already in epoll", __func__, event_fd(wev->data));
    }

    return NGX_OK;
}

int event_del_write(event_base_t *base, event_t *wev)
{
    wev->ready = NGX_FALSE;
	
    return event_delete(base, wev, EVENT_WRITE_EVENT, EVENT_CLEAR_EVENT);
}

