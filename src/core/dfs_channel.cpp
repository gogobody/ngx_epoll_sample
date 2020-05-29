#include "dfs_channel.h"
#include "dfs_conn.h"
#include "dfs_memory.h"

//复制信息，在发送
int channel_write(int socket, channel_t *ch, size_t size)
{
    ssize_t       n = 0;
    struct iovec  iov[1];
    struct msghdr msg;

    union 
	{
        struct cmsghdr cm;
        char           space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    if (ch->fd == -1) 
	{
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
    } 
	else 
	{
        msg.msg_control = (caddr_t) &cmsg;
        msg.msg_controllen = sizeof(cmsg);

        cmsg.cm.cmsg_len = CMSG_LEN(sizeof(int));
        cmsg.cm.cmsg_level = SOL_SOCKET;
        cmsg.cm.cmsg_type = SCM_RIGHTS;
        /*
         * have to use memory_memcpy() instead of simple
         * *(int *)CMSG_DATA(&cmsg.cm) = ch->fd;
         * because some gcc 4.4 with -O2/3/s optimization issues the warning:
         * dereferencing type-punned pointer will break strict-aliasing rules
         */
        //*(int *) CMSG_DATA(&cmsg.cm) = ch->fd;
        //赋值信息到 cmsg.cm
        memory_memcpy(CMSG_DATA(&cmsg.cm), &ch->fd, sizeof(int));
    }

    msg.msg_flags = 0;

    iov[0].iov_base = (char *) ch;
    iov[0].iov_len = size;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;


    n = sendmsg(socket, &msg, 0);

    if (n == NGX_ERROR)
	{
        if (errno == DFS_EAGAIN) 
		{
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}

// 接受到信息
int channel_read(int socket, channel_t *ch, size_t size)
{
    ssize_t       n = 0;
    struct iovec  iov[1];
    struct msghdr msg;

    union 
	{
        struct cmsghdr cm;
        char           space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    iov[0].iov_base = (char *) ch;
    iov[0].iov_len = size;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = (caddr_t) &cmsg;
    msg.msg_controllen = sizeof(cmsg);

    //接收消息
    n = recvmsg(socket, &msg, 0);

    if (n == NGX_ERROR)
	{
        if (errno == DFS_EAGAIN) 
		{
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }

    if (n == 0) 
	{
        return NGX_ERROR;
    }

    if ((size_t) n < sizeof(channel_t)) 
	{
        return NGX_ERROR;
    }

    if (ch->command == CHANNEL_CMD_OPEN) 
	{
        if (cmsg.cm.cmsg_len < (socklen_t) CMSG_LEN(sizeof(int))) 
		{
            return NGX_ERROR;
        }

        if (cmsg.cm.cmsg_level != SOL_SOCKET || cmsg.cm.cmsg_type != SCM_RIGHTS)
        {
            return NGX_ERROR;
        }

        //ch->fd = *(int *) CMSG_DATA(&cmsg.cm);
        //复制 fd
        memory_memcpy(&ch->fd, CMSG_DATA(&cmsg.cm), sizeof(int));
    }

    return n;
}

void channel_close(int *fd)
{
    close(fd[0]);
    close(fd[1]);
}

