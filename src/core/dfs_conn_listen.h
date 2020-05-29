#ifndef DFS_CONN_LISTEN_H
#define DFS_CONN_LISTEN_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "dfs_types.h"
#include "dfs_string.h"
#include "dfs_array.h"
#include "dfs_conn.h"
#include "dfs_error_log.h"

// ngx_listening_s 这个结构体在Nginx中用来监听一个端口
struct listening_s 
{
    int                    fd;   //socket套接字句柄
    struct sockaddr       *sockaddr;   //监听sockaddr地址
    socklen_t              socklen;    // size of sockaddr //sockaddr地址长度
    string_t               addr_text;  //以字符串形式储存IP地址
    int                    family;
    int                    type;      //套接字类型
    int                    backlog; /* TCP实现监听时的backlog队列，它表示允许正在通过三次握手建立连接但还未任何进程开始处理连接的最大数 */
    int                    rcvbuf; //内核中对于这个套接字的接收缓冲区大小
    int                    sndbuf; //内核中对这个套接字的发送缓冲区大小
    event_handler_pt       handler; //handler of accepted connection //新的TCP连接成功后的处理方法
    log_t                 *log;    //log和logp都是可用日志对象指针
    size_t                 conn_psize;  //为新的TCP连接创建内存池的大小
    listening_t           *previous;  //指向前一个ngx_listening_t结构
    conn_t                *connection; //当前监听句柄对应着的ngx_connection_t结构体
    uint32_t               open:1;   //1：当前监听句柄有效；0：正常关闭
    uint32_t               ignore:1;   //1:跳过设置当前ngx_listening_t结构体中的套接字；0：正常初始化
    uint32_t               linger:1; 
    uint32_t               inherited:1;  //说明是热升级过程
    uint32_t               listen:1;  //1：已开始监听
};

int conn_listening_open(array_t *listening, log_t *log);
listening_t * conn_listening_add(array_t *listening, pool_t *pool, 
    log_t *log, in_addr_t addr, in_port_t port, event_handler_pt handler,
    int rbuff_len, int sbuff_len);
int conn_listening_close(array_t *listening);
int conn_listening_add_event(event_base_t *base, array_t *listening);
int conn_listening_del_event(event_base_t *base, array_t *listening);

#endif

