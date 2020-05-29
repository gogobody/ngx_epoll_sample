#ifndef DFS_EPOLL_H
#define DFS_EPOLL_H

#include "dfs_types.h"
#include "dfs_queue.h"
#include "dfs_error_log.h"

typedef struct epoll_event epoll_event_t;

typedef void (*time_update_ptr)();
//niginx是通过将获取的事件先不调用其回调，而是把他们先放入俩个post队列，这俩个队列分别为
//
//.ngx_posted_accept_events
//.ngx_posted_events
//        第一个队列用来保存连接事件，而第二个队列用来保存普通读写事件，
//        之后在执行时我们可以先保证ngx_posted_accept_events中的事件先处理，就可以保证连接对响应速度的敏感性
struct event_base_s 
{
    int             ep;// epoll的句柄
    uint32_t        event_flags;
    epoll_event_t  *event_list;
    uint32_t        nevents; // max epoll event
    time_update_ptr time_update;
    queue_t         posted_accept_events; //连接事件
    queue_t         posted_events; //普通读写事件
    log_t           *log;
};

int  epoll_init(event_base_t *ep_base, log_t *log);
void epoll_done(event_base_t *ep_base);
int  epoll_add_event(event_base_t *ep_base, event_t *ev, int event, uint32_t flags);
int  epoll_del_event(event_base_t *ep_base,event_t *ev, int event, uint32_t flags);
int  epoll_add_connection(event_base_t *ep_base, conn_t *c);
int  epoll_del_connection(event_base_t *ep_base, conn_t *c, uint32_t flags);
int  epoll_process_events(event_base_t *ep_base,
    rb_msec_t timer, uint32_t flags);

#endif

