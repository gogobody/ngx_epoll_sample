#include "dfs_event_timer.h"

int event_timer_init(event_timer_t *timer, curtime_ptr handler, log_t *log)
{
    // 初始化 红黑树
    rbtree_init(&timer->timer_rbtree, &timer->timer_sentinel, 
		rbtree_insert_timer_value);
    timer->time_handler = handler;
    timer->log = log;
	
    return NGX_OK;
}

void event_timers_expire(event_timer_t *timer)
{
    event_t       *ev = nullptr;
    rbtree_node_t *node = nullptr;
    rbtree_node_t *root = nullptr;
    rbtree_node_t *sentinel = nullptr;

    for ( ;; ) 
	{
        sentinel = timer->timer_rbtree.sentinel; //用红黑树的哨兵
        root = timer->timer_rbtree.root;

        if (root == sentinel) 
		{
            return;
        }

        node = rbtree_min(root, sentinel);

        if (node->key <= timer->time_handler())  // 如果超时了
		{
            // 由于在插入红黑树时插入的是ev->timer，所以这个地方需要偏移来得到完整的event*
            ev = (event_t *) ((char *) node - offsetof(event_t, timer));

            rbtree_delete(&timer->timer_rbtree, &ev->timer); // 删除定时器

            ev->timer_set = 0; // 状态置为 0
            ev->timedout = 1; // 设为超时

            ev->handler(ev); //执行定时操作

            continue;
        }

        break;
    }
}

rb_msec_t event_find_timer(event_timer_t *ev_timer)
{
    rb_msec_int_t  timer = 0;
    rbtree_node_t *node = nullptr;
    rbtree_node_t *root = nullptr;
    rbtree_node_t *sentinel = nullptr;

    if (ev_timer->timer_rbtree.root == &ev_timer->timer_sentinel)
	{
        return EVENT_TIMER_INFINITE;
    }

    root = ev_timer->timer_rbtree.root;
    sentinel = ev_timer->timer_rbtree.sentinel;
    node = rbtree_min(root, sentinel);

    timer = node->key - ev_timer->time_handler();

    return (timer > 0 ? timer : 0);
}

void event_timer_del(event_timer_t *ev_timer, event_t *ev)
{
    if (!ev->timer_set) 
	{
        return;
    }

    dfs_log_debug(ev_timer->log, DFS_LOG_DEBUG, 0, "delete timer: %p, event:%p",
        &ev->timer, ev);

    rbtree_delete(&ev_timer->timer_rbtree, &ev->timer);

    ev->timer_set = 0;
}

//timer说白了就是一个int的值，表示超时的事件，用于表示红黑树节点的key
void event_timer_add(event_timer_t *ev_timer, event_t *ev, rb_msec_t timer)
{
    rb_msec_t     key;
    rb_msec_int_t diff;

    key = ev_timer->time_handler() + timer;
    /*当前时间 + 定时时间，这个值是固定的，表示将来的某一时刻，作为红黑树中的key*/
    if (ev->timer_set)  //如果该时间已经设置了定时器
	{
        /*
         * Use a previous timer value if difference between it and a new
         * value is less than EVENT_TIMER_LAZY_DELAY milliseconds: this allows
         * to minimize the rbtree operations for dfs connections.
         */
        diff = (rb_msec_int_t) (key - ev->timer.key);//如果距离超时时间 < 300ms，此时无需添加新的定时器，用已有的定时器就可以了
        if (abs(diff) < EVENT_TIMER_LAZY_DELAY) 
		{
            return;
        }
		
        event_timer_del(ev_timer, ev);
    }

    ev->timer.key = key;

    // 新节点插入红黑树，注意的是我们并没有插入整个event，而是插入了ev->timer
    rbtree_insert(&ev_timer->timer_rbtree, &ev->timer);

    dfs_log_debug(ev_timer->log, DFS_LOG_DEBUG, 0, "add timer: ev: %p, timer:%p",
        ev, &ev->timer);

    ev->timer_set = 1; // 状态置为1
}

