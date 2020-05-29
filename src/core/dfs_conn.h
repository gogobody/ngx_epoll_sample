#ifndef DFS_CONN_H
#define DFS_CONN_H

#include "dfs_types.h"
#include "dfs_sysio.h"
#include "dfs_string.h"
#include "dfs_event.h"
#include "dfs_error_log.h"

//#define CONN_DEFAULT_RCVBUF    -1
//#define CONN_DEFAULT_SNDBUF    -1
#define CONN_DEFAULT_RCVBUF    (64<<10)
#define CONN_DEFAULT_SNDBUF    (64<<10)
#define CONN_DEFAULT_POOL_SIZE 2048
#define CONN_DEFAULT_BACKLOG   2048

typedef struct conn_peer_s conn_peer_t;

enum 
{
    CONN_TCP_NODELAY_UNSET = 0,
    CONN_TCP_NODELAY_SET,
    CONN_TCP_NODELAY_DISABLED
};

enum 
{
    CONN_TCP_NOPUSH_UNSET = 0,
    CONN_TCP_NOPUSH_SET,
    CONN_TCP_NOPUSH_DISABLED
};

enum 
{
    CONN_ERROR_NONE = 0,
    CONN_ERROR_REQUEST,
    CONN_ERROR_EOF
};

// 这种连接是指 客户端发起的，服务器被动接受的连接
struct conn_s 
{
    int                    fd; //
    void                  *next;
    void                  *conn_data; // thread dn_request_t // in nn node nn_conn_s
    event_t               *read;  // 连接对应的读事件
    event_t               *write;
    sysio_recv_pt          recv;   //直接接收网络字符流的方法
    sysio_send_pt          send;   // 直接发送网络字符流的办法
    sysio_recv_chain_pt    recv_chain;  // 以ngx_chain_t链表为 参数来 接收 网络 字符流的方法
    sysio_send_chain_pt    send_chain;  // 以ngx_chain_t链表为 参数来 发送 网络 字符流的方法
    sysio_sendfile_pt      sendfile_chain;
    listening_t           *listening;  /*这个连接对应的ngx_listening_t监听对象，此连接由listening监听端口的事件建立*/
    size_t                 sent; //这个连接上已经发送出去的字节数
    pool_t                *pool;  /* 内存池。一般在accept一个新连接时，会创建一个 内存池，而在这个 连接结束时会销毁内存池。所有的ngx_connectionn_t结构 体都是预分配，这个内存池的大小将由上面的listening 监听对象中的 pool_size成员决定*/
    struct sockaddr       *sockaddr;
    socklen_t              socklen;
    string_t               addr_text; // 连接客户端字符串形式的IP地址
    struct timeval         accept_time;
    uint32_t               error:1;
    uint32_t               sendfile:1;
    uint32_t               sndlowat:1;
    uint32_t               tcp_nodelay:2;
    uint32_t               tcp_nopush:2;
    event_timer_t         *ev_timer;
    event_base_t          *ev_base;  
    log_t                 *log;
};

// 主动连接
struct conn_peer_s 
{
    conn_t                *connection;  /*一个主动连接实际 上也需要ngx_connection_t结构体的大部分成员，并且处于重用的考虑 而定义 了connecion*/
    struct sockaddr       *sockaddr;  // 远端服务器的socketaddr
    socklen_t              socklen;  // sockaddr地址长度
    string_t              *name;   // 远端服务器的名称
    int                    rcvbuf;
};

#define DFS_EPERM         EPERM
#define DFS_ENOENT        ENOENT
#define DFS_ESRCH         ESRCH
#define DFS_EINTR         EINTR
#define DFS_ECHILD        ECHILD
#define DFS_ENOMEM        ENOMEM
#define DFS_EACCES        EACCES
#define DFS_EBUSY         EBUSY
#define DFS_EEXIST        EEXIST
#define DFS_ENOTDIR       ENOTDIR
#define DFS_EISDIR        EISDIR
#define DFS_EINVAL        EINVAL
#define DFS_ENOSPC        ENOSPC
#define DFS_EPIPE         EPIPE
#define DFS_EAGAIN        EAGAIN
#define DFS_EINPROGRESS   EINPROGRESS
#define DFS_EADDRINUSE    EADDRINUSE
#define DFS_ECONNABORTED  ECONNABORTED
#define DFS_ECONNRESET    ECONNRESET
#define DFS_ENOTCONN      ENOTCONN
#define DFS_ETIMEDOUT     ETIMEDOUT
#define DFS_ECONNREFUSED  ECONNREFUSED
#define DFS_ENAMETOOLONG  ENAMETOOLONG
#define DFS_ENETDOWN      ENETDOWN
#define DFS_ENETUNREACH   ENETUNREACH
#define DFS_EHOSTDOWN     EHOSTDOWN
#define DFS_EHOSTUNREACH  EHOSTUNREACH
#define DFS_ENOSYS        ENOSYS
#define DFS_ECANCELED     ECANCELED
#define DFS_ENOMOREFILES  0

#define DFS_SOCKLEN       512

#define conn_nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK)
#define conn_blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)

int  conn_connect_peer(conn_peer_t *pc, event_base_t *ep_base);
conn_t *conn_get_from_mem(int s);
void conn_free_mem(conn_t *c);
void conn_set_default(conn_t *c, int s);
void conn_close(conn_t *c);
void conn_release(conn_t *c);
int  conn_tcp_nopush(int s);
int  conn_tcp_push(int s);
int  conn_tcp_nodelay(int s);
int  conn_tcp_delay(int s);

#endif

