#include <core/dfs_lock.h>
#include "dn_thread.h"
#include "dfs_memory.h"
#include "dfs_conn_listen.h"
#include "dn_time.h"
#include "dn_process.h"

extern _xvolatile rb_msec_t dfs_current_msec;
static pthread_key_t dfs_thread_key;
dfs_atomic_lock_t accept_lock;
volatile pthread_t accept_lock_held = -1;
int accecpt_enable = NGX_TRUE;

extern uint32_t  process_doing;
extern int       process_slot;

static int event_trylock_accept_lock(dfs_thread_t *thread);
static int event_free_accept_lock(dfs_thread_t *thread);

void thread_env_init()
{
    pthread_key_create(&dfs_thread_key, nullptr); //分配用于标识进程中线程特定数据的键。
}

dfs_thread_t *thread_new(pool_t *pool)
{
    if (pool) 
	{
        return (dfs_thread_t *)pool_calloc(pool, sizeof(dfs_thread_t));
    }
	
    return (dfs_thread_t *)memory_calloc(sizeof(dfs_thread_t));
}

void thread_bind_key(dfs_thread_t *thread)
{
    pthread_setspecific(dfs_thread_key, thread);
}

dfs_thread_t *get_local_thread()
{
    return (dfs_thread_t *)pthread_getspecific(dfs_thread_key);
}

event_base_t *thread_get_event_base()
{
    //同一线程内的各个函数间共享数据
    dfs_thread_t *thread = (dfs_thread_t *)pthread_getspecific(dfs_thread_key);
	
    return thread != nullptr ? &thread->event_base : nullptr;
}

event_timer_t *thread_get_event_timer()
{
    dfs_thread_t *thread = (dfs_thread_t *)pthread_getspecific(dfs_thread_key);
	
    return thread != nullptr ? &thread->event_timer : nullptr;
}

conn_pool_t * thread_get_conn_pool()
{
    dfs_thread_t *thread = (dfs_thread_t *)pthread_getspecific(dfs_thread_key);
	
    return &thread->conn_pool;
}

int thread_create(void *args)
{
    pthread_attr_t  attr;
    int             ret;
    dfs_thread_t   *thread = (dfs_thread_t *)args;

    pthread_attr_init(&attr);
    
    if ((ret = pthread_create(&thread->thread_id, &attr, 
		thread->run_func, thread)) != 0) 
    {
        dfs_log_error(dfs_cycle->error_log, DFS_LOG_FATAL, 0,
            "thread_create err: %s\n", strerror(ret));
		
        return NGX_ERROR;
    }

    return NGX_OK;
}

void thread_clean(dfs_thread_t *thread)
{
}

// 初始化 thread 的event
int thread_event_init(dfs_thread_t *thread)
{
    // 初始化 epoll句柄和 event list 空间分配
    if (epoll_init(&thread->event_base, dfs_cycle->error_log) == NGX_ERROR)
	{
        return NGX_ERROR;
    }

    event_timer_init(&thread->event_timer, time_curtime, dfs_cycle->error_log);

    //niginx是通过将获取的事件先不调用其回调，而是把他们先放入俩个post队列，这俩个队列分别为
	queue_init((queue_t *)&thread->posted_accept_events);
    queue_init((queue_t *)&thread->posted_events);


    return NGX_OK;
}

// 线程 event 处理
void thread_event_process(dfs_thread_t *thread)
{
    uint32_t      flags = EVENT_UPDATE_TIME;
    rb_msec_t     timer = 0;
    rb_msec_t     delta = 0;
    event_base_t *ev_base = nullptr;
	array_t      *listens = nullptr;
    
    ev_base = &thread->event_base;
//	listens = cycle_get_listen_for_cli(); // 所有cli的listening
    if (THREAD_CLI_TEST == thread->type)
    {
        flags = EVENT_POST_EVENTS | EVENT_UPDATE_TIME;
    }
//	if ((!(process_doing & PROCESS_DOING_QUIT))
//        && (!(process_doing & PROCESS_DOING_TERMINATE)))
//    {
//        if (thread->type == THREAD_WORKER // 抢锁
//			&& event_trylock_accept_lock(thread))
//		{
//        }
//    }
//
//	if (accecpt_enable && accept_lock_held == thread->thread_id &&
//        ((process_doing & PROCESS_DOING_QUIT) ||
//        (process_doing & PROCESS_DOING_TERMINATE)))
//    {
////        conn_listening_del_event(ev_base, listens); // 删除listening 事件
//        accecpt_enable = NGX_FALSE;
//        event_free_accept_lock(thread);
//    }

//	// add epoll event
//	// add listening event
//	if (accept_lock_held == thread->thread_id // 添加listening 事件
//        && conn_listening_add_event(ev_base, listens) == NGX_OK)
//    {
//        flags |= EVENT_POST_EVENTS;
//    }
//	else
//    {
//        event_free_accept_lock(thread);
//    }

	//
    timer = event_find_timer(&thread->event_timer);

    if ((timer > 10) || (timer == EVENT_TIMER_INFINITE)) 
	{
        timer = 10;
    }
    
    delta = dfs_current_msec;
    //
    (void) epoll_process_events(ev_base, timer, flags);

    /*ngx_posted_accept_events是一个事件队列
      暂存epoll从监听套接口wait到的accept事件。
      前文提到的NGX_POST_EVENTS标志被使用后，就会将
      所有的accept事件暂存到这个队列。

      这里完成对队列中的accept事件的处理，实际就是调用
      ngx_event_accept函数来获取一个新的连接，然后放入
      epoll中。
    */
    if ((THREAD_CLI_TEST == thread->type)
		&& !queue_empty(&ev_base->posted_accept_events)) 
    {
        event_process_posted(&ev_base->posted_accept_events, ev_base->log);
    }
//    /*所有accept事件处理完成，如果拥有锁的话，就赶紧释放了。
//      其他进程还等着抢了。
//    */
//    if (accept_lock_held == thread->thread_id)
//	{
//        conn_listening_del_event(ev_base, listens);
//        event_free_accept_lock(thread);
//    }
    /*处理普通事件（连接上获得的读写事件）队列上的所有事件，
      因为每个事件都有自己的handler方法，该怎么处理事件就
      依赖于事件的具体handler了。
    */
    if ((THREAD_CLI_TEST == thread->type)
		&& !queue_empty(&ev_base->posted_events)) 
    {
        event_process_posted(&ev_base->posted_events, ev_base->log);
    }

    delta = dfs_current_msec - delta;
    if (delta) 
	{
        event_timers_expire(&thread->event_timer);
    }

	if (THREAD_WORKER == thread->type) 
	{
//		cfs_ioevents_process_posted(&thread->io_events, &thread->fio_mgr);
    }
}

void accept_lock_init()
{
    accept_lock.lock = DFS_LOCK_OFF;
    accept_lock.allocator = nullptr;
}

static int event_trylock_accept_lock(dfs_thread_t *thread)
{
    dfs_lock_errno_t flerrno;

    if (dfs_atomic_lock_try_on(&accept_lock, &flerrno) == DFS_LOCK_ON)
	{
        accept_lock_held = thread->thread_id;
    }
    
    return NGX_OK;
}

static int event_free_accept_lock(dfs_thread_t *thread)
{
    dfs_lock_errno_t flerrno;
    
    if (accept_lock_held == -1U || accept_lock_held != thread->thread_id) 
	{
        return NGX_OK;
    }
    
    accept_lock_held = -1U;

    dfs_atomic_lock_off(&accept_lock, &flerrno);

    return NGX_OK;
}


