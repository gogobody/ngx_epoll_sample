#include "dfs_conn_pool.h"
#include "dfs_memory.h"
#include "dfs_event_timer.h"
#include "dfs_lock.h"
#include "dfs_conn.h"

conn_pool_t       comm_conn_pool;
dfs_atomic_lock_t comm_conn_lock;

static void put_comm_conn(conn_t *c);
static conn_t* get_comm_conn(uint32_t n, int *num);

// 初始化线程连接池
int conn_pool_init(conn_pool_t *pool, uint32_t connection_n)
{
    conn_t   *conn = nullptr;
    event_t  *revs = nullptr;
    event_t  *wevs = nullptr;
    uint32_t  i = 0;

    if (connection_n == 0) 
	{
        return NGX_ERROR;
    }

    pool->connection_n = connection_n;
    pool->connections = (conn_t *)memory_calloc(sizeof(conn_t) * pool->connection_n);// 为connections分配内存
    if (!pool->connections) 
	{
        return NGX_ERROR;
    }
    // 为read_events分配内存
    pool->read_events = (event_t *)memory_calloc(sizeof(event_t) * pool->connection_n);
    if (!pool->read_events) 
	{
        conn_pool_free(pool);
		
        return NGX_ERROR;
    }
    // 为write_events分配内存
    pool->write_events = (event_t *)memory_calloc(sizeof(event_t) * pool->connection_n);
    if (!pool->write_events) 
	{
        conn_pool_free(pool);
		
        return NGX_ERROR;
    }

    conn = pool->connections;
    revs = pool->read_events;
    wevs = pool->write_events;

    for (i = 0; i < pool->connection_n; i++) 
	{
        revs[i].instance = 1;

        if (i == pool->connection_n - 1) 
		{
            conn[i].next = nullptr;
        } 
		else 
		{
            conn[i].next = &conn[i + 1];
        }
        // 每个connection，对应一个read_events, 一个write_events
        conn[i].fd = NGX_INVALID_FILE;
        conn[i].read = &revs[i];
        conn[i].read->timer_event = NGX_FALSE;
        conn[i].write = &wevs[i];
        conn[i].write->timer_event = NGX_FALSE;
    }

    pool->free_connections = conn;
    pool->free_connection_n = pool->connection_n;

    return NGX_OK;
}

void conn_pool_free(conn_pool_t *pool)
{
    if (!pool) 
	{
        return;
    }

    if (pool->connections) 
	{
        memory_free(pool->connections);
        pool->connections = nullptr;
    }

    if (pool->read_events) 
	{
        memory_free(pool->read_events);
        pool->read_events = nullptr;
    }

    if (pool->write_events) 
	{
        memory_free(pool->write_events);
        pool->write_events = nullptr;
    }

    pool->connection_n = 0;
    pool->free_connection_n = 0;
}

conn_t * conn_pool_get_connection(conn_pool_t *pool) {
    conn_t *c = nullptr;
    int num = 0;
    uint32_t n = 0;

    c = pool->free_connections;
    // 当free_connection中没有可以用的connection时，扫描连接池，找到一个可以用的
    if (c == nullptr) {
        if (pool->change_n >= 0) {
            return nullptr;
        }
        n = -pool->change_n;
        c = get_comm_conn(n, &num);
        if (c) {
            pool->free_connections = c;
            pool->free_connection_n += num;
            pool->change_n += num;

            goto out_conn;
        }
        return nullptr;

        out_conn:
        pool->free_connections = (conn_t *) c->next; // 指向下一个connections
        pool->free_connection_n--;    // 空闲connection数-1
        pool->used_n++;

        return c;
    }
    // if c then out conn
    goto out_conn;
}

void conn_pool_free_connection(conn_pool_t *pool, conn_t *c)
{   
    if (pool->change_n > 0) 
	{
        pool->used_n--;
        put_comm_conn(c);
		pool->change_n--;
		
        return;
    }
	
    c->next = pool->free_connections;

    pool->free_connections = c;
    pool->free_connection_n++;
	pool->used_n--;

}

int conn_pool_common_init()
{
    comm_conn_lock.lock = DFS_LOCK_OFF;
    comm_conn_lock.allocator = nullptr;
    memset(&comm_conn_pool, 0, sizeof(conn_pool_t));
	
    return NGX_OK;
}
int conn_pool_common_release(){
    comm_conn_lock.lock = DFS_LOCK_OFF;
    comm_conn_lock.allocator = nullptr;
	
    return NGX_OK;
}

static void put_comm_conn(conn_t *c)
{
	dfs_lock_errno_t error;
   
    dfs_atomic_lock_on(&comm_conn_lock, &error);
    comm_conn_pool.free_connection_n++;
    c->next = comm_conn_pool.free_connections;
    comm_conn_pool.free_connections = c;
    
    dfs_atomic_lock_off(&comm_conn_lock, &error);
}

static conn_t* get_comm_conn(uint32_t n, int *num)
{
	dfs_lock_errno_t  error;
    conn_t           *c = nullptr, *p = nullptr, *plast = nullptr;
    uint32_t          i = 0;
		
	dfs_atomic_lock_on(&comm_conn_lock, &error);
	
    if (!comm_conn_pool.free_connection_n) 
	{
        dfs_atomic_lock_off(&comm_conn_lock, &error);
		
        return nullptr;
    }

    if (n >= comm_conn_pool.free_connection_n) 
	{
        c = comm_conn_pool.free_connections;
        *num = comm_conn_pool.free_connection_n;

        comm_conn_pool.free_connections = nullptr;
        comm_conn_pool.free_connection_n = 0;

        dfs_atomic_lock_off(&comm_conn_lock, &error);
		
        return c;
    }
	
	p = comm_conn_pool.free_connections;
    i = n;
	
	for (plast = p; p && i > 0; i--) 
	{
		plast = p;
		p = (conn_t *)p->next;
	}
	
	c = comm_conn_pool.free_connections;
    
	comm_conn_pool.free_connections = p;
    comm_conn_pool.free_connection_n -= n;
    
	plast->next = nullptr;
    *num = n;
    
    dfs_atomic_lock_off(&comm_conn_lock, &error);

    return c;
}

void conn_pool_out(conn_pool_t *pool, int n) 
{
	pool->change_n -= n; 
	pool->used_n -=n; 
}
		
void conn_pool_in(conn_pool_t *pool, int n) 
{
	pool->change_n += n; 
	pool->used_n += n; 
}

