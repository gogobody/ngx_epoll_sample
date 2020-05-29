#include "dfs_sysio.h"
#include "dfs_chain.h"

static ssize_t sysio_writev_iovs(conn_t *c, sysio_vec *iovs, int count);
static int sysio_pack_chain_to_iovs(sysio_vec *iovs, int iovs_count, 
	chain_t *in, size_t *last_size, size_t limit);

sysio_t linux_io = 
{
    // read
    sysio_unix_recv,
    sysio_readv_chain,
    sysio_udp_unix_recv,
    // write
    sysio_unix_send,
    sysio_writev_chain,
    sysio_sendfile_chain,
    0
};

//
ssize_t sysio_unix_recv(conn_t *c, uchar_t *buf, size_t size)
{
    ssize_t  n = 0;

    if (!c) 
	{
        dfs_log_error(c->log, DFS_LOG_WARN, 0,
            "sysio_unix_recv: c is nullptr");
		
        return NGX_ERROR;
    }
	
    if (!buf) 
	{
        dfs_log_error(c->log, DFS_LOG_WARN, 0,
            "sysio_unix_recv: buf is nullptr");
		
        return NGX_ERROR;
    }
	
    for (;;) 
	{
        errno = 0;
		
        n = recv(c->fd, buf, size, 0);
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_unix_recv: recv:%d, buf size:%d, fd:%d",
            n, size, c->fd);

        if (n > 0) 
		{
            return n;
        }

        if (n == 0) 
		{
            return n;
        }

        if (errno == DFS_EINTR) 
		{
            continue;
        }
		
        if (errno == DFS_EAGAIN) 
		{
            dfs_log_debug(c->log,DFS_LOG_DEBUG, 0,
                "sysio_unix_recv: not ready");
			
            return NGX_AGAIN;
        }
 
        dfs_log_error(c->log, DFS_LOG_ERROR, errno,
            "sysio_unix_recv: error, fd:%d", c->fd);
		
        return NGX_ERROR;
    }

    return NGX_OK;
}

ssize_t sysio_readv_chain(conn_t *c, chain_t *chain)
{
    int           i = 0;
    uchar_t      *prev = nullptr;
    ssize_t       n = 0;
    ssize_t       size = 0;
    struct iovec  iovs[DFS_IOVS_REV];

    while (chain && i < DFS_IOVS_REV) //遍历chain缓冲链表，不断的申请struct iovec结构为待会的readv做准备，碰到临近2块内存如果正好接在一起，就公用之。
	{
        if (prev == chain->buf->last) //说明前面一个chain的end后和面一个chain的last刚好相等，也就是这两个chain内存是连续的 临近2块内存如果正好接在一起，就公用之
		{
            iovs[i - 1].iov_len += chain->buf->end - chain->buf->last;
			
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "sysio_readv_chain: readv prev == chain->buf->last");
        } 
		else 
		{
            iovs[i].iov_base = (void *) chain->buf->last;
            iovs[i].iov_len = chain->buf->end - chain->buf->last;
			
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "sysio_readv_chain: readv iov len %d", iovs[i].iov_len);
			
            i++;
        }
		
        size += chain->buf->end - chain->buf->last;//该chain->buf中可以使用的内存有这么多
        prev = chain->buf->end;
        chain = chain->next;
    }

    for (;;) 
	{
        errno = 0;
		
        n = readv(c->fd, iovs, i);
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_readv_chain: read:%d buf size:%d", n, size);

        if (n > 0) 
		{
            return n;
        }

        if (n == 0) 
		{
            return n;
        }

        if (errno == DFS_EAGAIN) 
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "sysio_readv_chain: not ready");
			
            return NGX_AGAIN;
        }
		
        if (errno == DFS_EINTR) 
		{
            continue;
        }
		
        dfs_log_error(c->log, DFS_LOG_WARN, errno,
            "sysio_readv_chain: read error");
		
        return NGX_ERROR;
    } 

    return NGX_OK;
}

ssize_t sysio_udp_unix_recv(conn_t *c, uchar_t *buf, size_t size)
{
    ssize_t n = 0;

    for (;;) 
	{
        errno = 0;
		
        n = recv(c->fd, buf, size, 0);
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_udp_unix_recv: recv:%d buf_size:%d fd:%d", n, size, c->fd);

        if (n >= 0) 
		{
            return n;
        }

        if (errno == DFS_EINTR) 
		{
            continue;
        }
		
        if (errno == DFS_EAGAIN) 
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, errno,
                "sysio_udp_unix_recv: not ready");
			
            return NGX_AGAIN;
        }

        dfs_log_error(c->log, DFS_LOG_WARN, errno,
            "sysio_udp_unix_recv: recv error");
		
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

ssize_t sysio_unix_send(conn_t *c, uchar_t *buf, size_t size)
{
    ssize_t  n = 0;
    event_t *wev = nullptr;

    wev = c->write;

    for ( ;; ) 
	{
        errno = 0;
		
        n = send(c->fd, buf, size, 0);
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_unix_send: send:%d buf_size:%d fd:%d",n, size, c->fd);

        if (n > 0) 
		{
            if (n < (ssize_t) size) 
			{
                wev->ready = 0;
            }
			
            return n;
        }

        if (n == 0) 
		{
            dfs_log_error(c->log, DFS_LOG_ALERT, errno,
                "sysio_unix_send: send zero, not ready");
			
            wev->ready = 0;
			
            return n;
        }

        if (errno == DFS_EINTR) 
		{
            continue;
        }
		
        if (errno == DFS_EAGAIN) 
		{
            wev->ready = 0;
			
            dfs_log_debug(c->log, DFS_LOG_DEBUG, errno,
                "sysio_unix_send: not ready");
			
            return NGX_AGAIN;
        }

        dfs_log_error(c->log, DFS_LOG_WARN, errno,
            "sysio_unix_send: send error");
		
        return NGX_ERROR;
    }

    return NGX_OK;
}

// ngx_writev_chain
//调用writev一次发送多个缓冲区，如果没有发送完毕，则返回剩下的链接结构头部。
//ngx_chain_writer调用这里，调用方式为 ctx->out = c->send_chain(c, ctx->out, ctx->limit);
chain_t * sysio_writev_chain(conn_t *c, chain_t *in, size_t limit)
{
    int        pack_count = 0;
    size_t     packall_size = 0;
    size_t     last_size = 0;
    ssize_t    sent_size = 0;
    chain_t   *cl = nullptr;
    event_t   *wev = nullptr;
    sysio_vec  iovs[DFS_IOVS_MAX];

    if (!in) 
	{
        return nullptr;
    }
	
    if (!c) 
	{
        return DFS_CHAIN_ERROR;
    }
	
    wev = c->write;
    if (!wev) 
	{
        return DFS_CHAIN_ERROR;
    }
	
    if (!wev->ready) 
	{
        return in;
    }
	
    // the maximum limit size is the 2G - page size
    if (limit == 0 || limit > DFS_MAX_LIMIT) 
	{
        limit = DFS_MAX_LIMIT;
    }
    
    while (in && packall_size < limit) 
	{
        last_size = packall_size; //last_size为上一次调用ngx_writev发送出去的字节数
		if (in->buf->memory == NGX_FALSE)
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "%s, file data break", __func__);
			
			break;
		}
        //把in链中的buf拷贝到vec->iovs[n++]中，注意只会拷贝内存中的数据到iovec中，不会拷贝文件中的
        //返回为ngx_output_chain_to_iovec中组包的in链中所有数据长度和

        // need recheck
        // iovs count
        pack_count = sysio_pack_chain_to_iovs(iovs,
            DFS_IOVS_MAX, in, &packall_size, limit);
        if (pack_count == 0) 
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "%s, pack_count zero", __func__);
			
            return nullptr;
        }
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_writev_chain: pack_count:%d, packall_size:%ul",
            pack_count, packall_size);


        // writev
        sent_size = sysio_writev_iovs(c, iovs, pack_count);
        //我期望发送vec->size字节数据，但是实际上内核发送出去的很可能比vec->size小，n为实际发送出去的字节数，因此需要继续发送
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "sysio_writev_chain: write:%d, iovs_size:%ul, sent:%d",
            sent_size, packall_size - last_size, c->sent);
		
        if (sent_size > 0) 
		{
            c->sent += sent_size;//递增统计数据，这个链接上发送的数据大小
            cl = chain_write_update(in, sent_size);//sent_size是此次调用ngx_wrtev发送成功的字节数
            //chain_write_update返回后的in链已经不包括之前发送成功的in节点了，这上面只包含剩余的数据
			
            if (packall_size - last_size > (size_t)sent_size) //这里说明最多调用ngx_writev两次成功发送后，这里就会返回
			{
                dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                    "sysio_writev_chain: write size < iovs_size");
            }
			
            if (packall_size >= limit) //数据发送完毕，或者本次发送成功的字节数比limit还多，则返回出去
			{
                dfs_log_debug(c->log, DFS_LOG_DEBUG, errno,
                    "sysio_writev_chain: writev to limit");
				
                return cl;
            }
			
            in = cl;
			
            continue;
        }
		
        if (sent_size == NGX_AGAIN)  //
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "%s, writev_chain again", __func__);
			
            wev->ready = 0;//标记暂时不能发送数据了，必须重新epoll_add写事件
			
            return in;
        }
		
        if (sent_size == NGX_ERROR)
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, errno,
                "sysio_writev_chain: writev error, fd:%d", c->fd);
			
            return DFS_CHAIN_ERROR;
        }
    }
    
    return in;
}

/*
 * On Linux up to 2.4.21 sendfile() (syscall #187) works with 32-bit
 * offsets only, and the including <sys/sendfile.h> breaks the compiling,
 * if off_t is 64 bit wide.  So we use own sendfile() definition, where offset
 * parameter is int32_t, and use sendfile() for the file parts below 2G only,
 * see src/os/unix/dfs_linux_config.h
 *
 * Linux 2.4.21 has the new sendfile64() syscall #239.
 *
 * On Linux up to 2.6.16 sendfile() does not allow to pass the count parameter
 * more than 2G-1 bytes even on 64-bit platforms: it returns EINVAL,
 * so we limit it to 2G-1 bytes.
 */
chain_t * sysio_sendfile_chain(conn_t *c, chain_t *in, 
                                     int fd, size_t limit)
{
	int      rc = 0;
    size_t   pack_size = 0;
    event_t *wev = nullptr;
    size_t   sent = 0;

	if (!in) 
	{
        return NULL;
    }
	
    if (!c) 
	{
        return DFS_CHAIN_ERROR;
    }
	
    wev = c->write;
    if (!wev) 
	{
        return DFS_CHAIN_ERROR;
    }
	
	if (!wev->ready) 
	{
        return in;
    }
	
    // the maximum limit size is the 2G - page size
    if (limit == 0 || limit > DFS_MAX_LIMIT) 
	{
        limit = DFS_MAX_LIMIT;
    }
	
    dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
        "%s, limit:%d, fd:%d", __func__, limit, fd);
	
    while (in && sent < limit) 
	{
		if (in->buf->memory == NGX_TRUE)
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "%s, memory data break", __func__);
			
			break;
		}
		
		pack_size = buffer_size(in->buf);
		if (pack_size == 0) 
		{
            dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                "%s, pack size zero", __func__); 
			in = in->next;
			
			continue;
		}
		
        if (sent + pack_size > limit) 
		{
           pack_size = limit - sent;
        }
		
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "%s, limit:%d already sent:%d pack_size:%d, file_pos:%l",
            __func__, limit, sent, pack_size, in->buf->file_pos);
		
		rc = sendfile(c->fd, fd, &in->buf->file_pos, pack_size);
		if (rc == NGX_ERROR)
		{
			if (errno == DFS_EAGAIN) 
			{
                dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                    "%s, sendfile again",__func__); 
				
				wev->ready = 0;
				
				return in;
		    } 
			else if (errno == DFS_EINTR) 
			{
                dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
                    "%s, sendfile interupt, continue",__func__);
				
	            continue;
	        } 
			
	    	dfs_log_error(c->log, DFS_LOG_ERROR, errno,
	        	"%s: sendfile error, fd:%d, fd:%d", __func__, c->fd, fd);
			
			return DFS_CHAIN_ERROR;
		}

        if (!rc) 
		{
            dfs_log_error(c->log, DFS_LOG_ALERT, 0,
                "%s: sendfile reture 0, file pos might have error", __func__);
			
            return DFS_CHAIN_ERROR;
        }
        
        dfs_log_debug(c->log, DFS_LOG_DEBUG, 0,
            "%s: fd:%d, sendfile:%d, pack_size:%d, c->sent:%d, file_pos:%l "
            "remain buf size:%l", __func__, c->fd, rc, pack_size, c->sent,
            in->buf->file_pos, buffer_size(in->buf));
		
        if (rc > 0) 
		{
            c->sent += rc;
            sent += rc;
			
			if (buffer_size(in->buf) == 0) 
			{
				in = in->next;
			}
        }
    }
 
    return in;
}

// 把in链中的buf拷贝到vec->iovs[n++]中，注意只会拷贝内存中的数据到iovec中，不会拷贝文件中的
// 似乎有点问题？
// https://github.com/y123456yz/reading-code-of-nginx-1.9.2/blob/d4211403a022a275dd8ed68530353a5df7a12a5c/nginx-1.9.2/src/os/unix/ngx_writev_chain.c
// ngx_output_chain_to_iovec
static int sysio_pack_chain_to_iovs(sysio_vec *iovs, int iovs_count, 
	                                         chain_t *in, size_t *last_size,
	                                         size_t limit)
{
    int      i = 0;
    ssize_t  bsize = 0;
    uchar_t *last_pos = NULL;

    if (!iovs || !in || !last_size) 
	{
        return i;
    }
	
    while (in && i < iovs_count && *last_size < limit) 
	{
		if (in->buf->memory == NGX_FALSE)
		{
			break;
		}
		
        bsize = buffer_size(in->buf);
        if (bsize <= 0) 
		{
            in = in->next;
			
            continue;
        }
		
        if (*last_size + bsize > limit) //超过最大发送大小。截断，这次只发送这么多
		{
            bsize = limit - *last_size;
        }
		
        if (last_pos != in->buf->pos) //要新增一个节点
		{
            iovs[i].iov_base = in->buf->pos; //从这里开始
            iovs[i].iov_len = bsize;//有这么多我要发送
            i++;
        }
		else //如果还是等于刚才的位置，那就复用 //
		{
            iovs[i - 1].iov_len += bsize;
        }
		
        *last_size += bsize;
        last_pos = in->buf->pos + bsize; // add bsize
        in = in->next;
    }

    return i;
}

static ssize_t sysio_writev_iovs(conn_t *c, sysio_vec *iovs, int count)
{
    ssize_t rc = NGX_ERROR;
    
    if (!c || !iovs || count <= 0) 
	{
        return rc;
    }
    
    for (;;) 
	{
        errno = 0;
		
        rc = writev(c->fd, iovs, count);
        if (rc > 0) 
		{
            return rc;
        }
		
        if (rc == NGX_ERROR)
		{
            if (errno == DFS_EINTR) 
			{
                continue;
            }
			
            if (errno == DFS_EAGAIN) 
			{
                return NGX_AGAIN;
            }
			
            return NGX_ERROR;
        }
		
        if (rc == 0) 
		{
            return NGX_ERROR;
        }
    }

    return NGX_ERROR;
}

