#include "dfs_epoll.h"
#include "dfs_memory.h"
#include "dfs_event.h"
#include "dfs_conn.h"

int epoll_init(event_base_t *ep_base, log_t *log)
{
    ep_base->ep = epoll_create(ep_base->nevents);
    if (ep_base->ep == NGX_INVALID_FILE)
	{
        dfs_log_error(log, DFS_LOG_EMERG,
            errno, "epoll_init: epoll_create failed");
		
        return NGX_ERROR;
    }

    ep_base->event_list = (epoll_event_t *)memory_calloc(
        sizeof(struct epoll_event) * ep_base->nevents);
    if (!ep_base->event_list) 
	{
        dfs_log_error(log, DFS_LOG_EMERG, 0,
            "epoll_init: alloc event_list failed");
		
        return NGX_ERROR;
    }

#if (EVENT_HAVE_CLEAR_EVENT)
    ep_base->event_flags = EVENT_USE_CLEAR_EVENT
#else
    ep_base->event_flags = EVENT_USE_LEVEL_EVENT
#endif
        |EVENT_USE_GREEDY_EVENT
        |EVENT_USE_EPOLL_EVENT;

    queue_init(&ep_base->posted_accept_events);
    queue_init(&ep_base->posted_events);
    ep_base->log = log;
	
    return NGX_OK;
}

void epoll_done(event_base_t *ep_base)
{
    if (close(ep_base->ep) == NGX_ERROR)
	{
        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll close() failed");
    }

    ep_base->ep = NGX_INVALID_FILE;
    if (ep_base->event_list) 
	{
        memory_free(ep_base->event_list);
        ep_base->event_list = nullptr;
    }

    ep_base->nevents = 0;
    ep_base->event_flags = 0;
}

/* epoll event
 * data.ptr is conn
 * */
// set event active true
int epoll_add_event(event_base_t *ep_base, event_t *ev, 
	                      int event, uint32_t flags)
{
    int                 op;
    conn_t             *c = nullptr;
    event_t            *aevent = nullptr;
    uint32_t            events;
    uint32_t            aevents;
    struct epoll_event  ee;
    
    memory_zero(&ee, sizeof(ee));
    c = (conn_t *)ev->data; // 获取到 event的connection
    events = (uint32_t) event;

    //所以nginx这里就是为了避免这种情况，当要在epoll中加入对一个fd读事件(即NGX_READ_EVENT)的监听时，
    //nginx先看一下与这个fd相关的写事件的状态，即e=c->write，如果此时e->active为1，
    // 说明该fd之前已经以NGX_WRITE_EVENT方式被加到epoll中了，此时只需要使用mod方式，将我们的需求加进去，
    // 否则才使用add方式，将该fd注册到epoll中。反之处理NGX_WRITE_EVENT时道理是一样的。

    if (event == EVENT_READ_EVENT) 
	{
        aevent = c->write;
        aevents = EPOLLOUT;
    } 
	else 
	{
        aevent = c->read;
        aevents = EPOLLIN;
    }
	
    // another event is active, don't forget it
    //  当一个fd第一次加入到epoll中的时候，active会被置1，意味着这个fd是有效的。
    //  直到我们把这个fd从epoll中移除，active才会清零。ready是另一层处理，这个fd虽然在epoll中，但是有时这个fd可以读写，
    //  有时则是未就绪的。那么当可读写时，ready就会被置1。这样我们就可以来读写数据了。
    //  当我们从fd读写到EAGAIN时，ready就会被清零，意味着当前这个fd未就绪。但是它不影响active，
    //  因为这个fd仍然在epoll中，ready==0只是要等待后续的读写触发。所以nginx在这两个变量的使用上是很明确的。

    if (aevent->active)
	{
        op = EPOLL_CTL_MOD;
        events |= aevents; // EPOLLIN | EPOLLOUT
    } 
	else 
	{
        op = EPOLL_CTL_ADD;
    }
	
    ee.events = events | (uint32_t) flags;
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance); // connection

    // 其实从nginx的设计上来讲，它想表达的语义很明确：
    //当一个fd第一次加入到epoll中的时候，active会被置1，意味着这个fd是有效的。直到我们把这个fd从epoll中移除，active才会清零。
    // ready是另一层处理，这个fd虽然在epoll中，但是有时这个fd可以读写，有时则是未就绪的。
    // 那么当可读写时，ready就会被置1。这样我们就可以来读写数据了。当我们从fd读写到EAGAIN时，ready就会被清零，意味着当前这个fd未就绪。
    // 但是它不影响active，因为这个fd仍然在epoll中，ready==0只是要等待后续的读写触发。
    ev->active = NGX_TRUE;

    if (epoll_ctl(ep_base->ep, op, c->fd, &ee) == -1)
	{
        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll_add_event: fd:%d op:%d, failed", c->fd, op);
        ev->active = NGX_FALSE;
		
        return NGX_ERROR;
    }

    ev->active = NGX_TRUE;

    return NGX_OK;
}

int epoll_del_event(event_base_t *ep_base, event_t *ev, 
	                     int event, uint32_t flags)
{
    int                 op;
    conn_t             *c = nullptr;
    event_t            *e = nullptr;
    uint32_t            prev;
    struct epoll_event  ee;
    
    memory_zero(&ee, sizeof(ee));

    c = (conn_t *)ev->data;
 
    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicity the event
     * before the closing the file descriptor
     */
    if (flags & EVENT_CLOSE_EVENT) 
	{
        ev->active = 0;
		
        return NGX_OK;
    }

    if (event == EVENT_READ_EVENT) 
	{
        e = c->write;
        prev = EPOLLOUT;
    } 
	else 
	{
        e = c->read;
        prev = EPOLLIN;
    }

    if (e->active) 
	{
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);
    } 
	else 
	{
        op = EPOLL_CTL_DEL;
        //ee.events = 0;
        ee.events = event;
        ee.data.ptr = nullptr;
    }

    if (epoll_ctl(ep_base->ep, op, c->fd, &ee) == -1) 
	{
        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll_ctl(%d, %d) failed", op, c->fd);
		
        return NGX_ERROR;
    }

    ev->active = 0;
	
    return NGX_OK;
}

int epoll_add_connection(event_base_t *ep_base, conn_t *c)
{
    struct epoll_event ee;
    
    if (!c) 
	{
        return NGX_ERROR;
    }

    memory_zero(&ee, sizeof(ee));
    ee.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    if (epoll_ctl(ep_base->ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) 
	{
        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
		
        return NGX_ERROR;
    }

    c->read->active = NGX_TRUE;
    c->write->active = NGX_TRUE;

    return NGX_OK;
}

int epoll_del_connection(event_base_t *ep_base, conn_t *c, uint32_t flags)
{
    int                op;
    struct epoll_event ee;
 
    if (!ep_base) 
	{
        return NGX_OK;
    }
	
    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicity the event
     * before the closing the file descriptor
     */
   
    if (flags & EVENT_CLOSE_EVENT) 
	{
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = nullptr;
	
    if (epoll_ctl(ep_base->ep, op, c->fd, &ee) == -1) 
	{
        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll_ctl(%d, %d) failed", op, c->fd);
		
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}

// epoll main func

// param : thread-> ep_base
// connection = ep_base ->event_list[i].data.ptr
// 添加事件到 ep_base 的event queue 或者执行对应event的 handler 函数
int epoll_process_events(event_base_t *ep_base, rb_msec_t timer, 
	                             uint32_t flags)
{
    int               i = 0;
    int               hd_num = 0;
    int               events_num = 0;
    uint32_t          events = 0;
    int               instance = 0;
    conn_t           *c = nullptr;
    event_t          *rev= nullptr;
    event_t          *wev = nullptr;
    volatile queue_t *queue = nullptr; // volatile 声明的变量的值的时候，系统总是重新从它所在的内存读取数据

    errno = 0;
    events_num = epoll_wait(ep_base->ep, ep_base->event_list,
        (int) ep_base->nevents, timer); // nevents 是每次能处理的事件数 timer -1相当于阻塞，0相当于非阻塞。

    if (flags & EVENT_UPDATE_TIME && ep_base->time_update)
	{
        ep_base->time_update();
    }

    if (events_num == -1)
	{
//        printf(errno);
        if(errno!=EINTR)
        {
            dfs_log_error(ep_base->log, DFS_LOG_EMERG, errno,
                          "epoll_process_events: epoll_wait failed");

            return NGX_ERROR;
        }
        return NGX_OK;
    }

    if (events_num == 0)
	{
        if (timer != EVENT_TIMER_INFINITE)
		{
            return NGX_OK;
        }

        dfs_log_error(ep_base->log, DFS_LOG_ALERT, errno,
            "epoll_process_events: epoll_wait no events or timeout");

        return NGX_ERROR;
    }

    for (i = 0; i < events_num; i++)
	{
        c = (conn_t *)ep_base->event_list[i].data.ptr;
        instance = (uintptr_t) c & 1;
        c = (conn_t *) ((uintptr_t) c & (uintptr_t) ~1); // 不同系统适配，32位机，64位机

        rev = c->read;
        /*
          fd在当前处理时变成-1，意味着在之前的事件处理时，把当前请求关闭了，
          即close fd并且当前事件对应的连接已被还回连接池，此时该次事件就不应该处理了，作废掉。
          其次，如果fd > 0,那么是否本次事件就可以正常处理，就可以认为是一个合法的呢？答案是否定的。
          这里我们给出一个情景：
          当前的事件序列是： A ... B ... C ...
          其中A,B,C是本次epoll上报的其中一些事件，但是他们此时却相互牵扯：
          A事件是向客户端写的事件，B事件是新连接到来，C事件是A事件中请求建立的upstream连接，此时需要读源数据，
          然后A事件处理时，由于种种原因将C中upstream的连接关闭了(比如客户端关闭，此时需要同时关闭掉取源连接)，自然
          C事件中请求对应的连接也被还到连接池(注意，客户端连接与upstream连接使用同一连接池)，
          而B事件中的请求到来，获取连接池时，刚好拿到了之前C中upstream还回来的连接结构，当前需要处理C事件的时候，
          c->fd != -1，因为该连接被B事件拿去接收请求了，而rev->instance在B使用时，已经将其值取反了，所以此时C事件epoll中
          携带的instance就不等于rev->instance了，因此我们也就识别出该stale event，跳过不处理了。
         */
        // 防止“过期事件”
        if (c->fd == NGX_INVALID_FILE || rev->instance != instance)
		{
            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */
            dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                "epoll_process_events: stale event %p", c);

            continue;
        }

        events = ep_base->event_list[i].events;
        if (events & (EPOLLERR|EPOLLHUP))
		{
            dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, errno,
                "epoll_process_events: epoll_wait error on fd:%d ev:%ud",
                c->fd, events);
        }

        if ((events & (EPOLLERR|EPOLLHUP))
             && (events & (EPOLLIN|EPOLLOUT)) == 0)
        {
            /*
             * if the error events were returned without
             * EPOLLIN or EPOLLOUT, then add these flags
             * to handle the events at least in one active handler
             */
            events |= EPOLLIN|EPOLLOUT;
        }

        // read event
        // active 在 add的时候设置为 true
        if ((events & EPOLLIN) && rev->active)
		{
            // 设置 event ready
            rev->ready = NGX_TRUE;

            if (!rev->handler)
			{
                dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                    "epoll_process_events: rev->handler nullpt");

                continue;
            }

            // THREAD_DN or THREAD_CLI_TEST
            if (flags & EVENT_POST_EVENTS) // accept events
			{
                queue = rev->accepted ? &ep_base->posted_accept_events:
                                        &ep_base->posted_events;
                rev->last_instance = instance; // no use

                dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                    "epoll_process_events: post read event, fd:%d", c->fd);
				// EVENT_POST_EVENTS
				// rev->post_queue 添加到 ep_base 的 events queue
                queue_insert_tail((queue_t*)queue, &rev->post_queue);
            }
            else  // other thread handle events
			{
                dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                    "epoll_process_events: read event fd:%d", c->fd);

                rev->handler(rev);
                hd_num++; // no use
            }
        }

        wev = c->write;

        if ((events & EPOLLOUT) && wev->active)
		{
            // 设置 event ready
            wev->ready = NGX_TRUE;

            if (!wev->handler)
			{
                dfs_log_error(ep_base->log, DFS_LOG_WARN, 0,
                    "epoll_process_events: wev->handler nullpt");
				
                continue;
            }
            // THREAD_DN or THREAD_CLI_TEST
            if (flags & EVENT_POST_EVENTS) 
			{
                dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                    "epoll_process_events: post write event, fd:%d", c->fd);
				
                rev->last_instance = instance;
                // add post event queue
                queue_insert_tail((queue_t*)&ep_base->posted_events, &wev->post_queue);
            } 
			else 
			{
                dfs_log_debug(ep_base->log, DFS_LOG_DEBUG, 0,
                    "epoll_process_events: write event fd:%d", c->fd);
				
                wev->handler(wev);
                hd_num++;// no use
            }
        }
    }

    return NGX_OK;
}

