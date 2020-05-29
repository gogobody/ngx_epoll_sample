#ifndef DFS_CHANNEL_H
#define DFS_CHANNEL_H

#include "dfs_types.h"
#include "dfs_event.h"

typedef struct channel_s 
{
     uint32_t command;// 传递的 TCP 消息中的命令
     pid_t    pid;// 进程 ID，一般是发送命令方的进程 ID
     int      slot;// 表示发送命令方在 ngx_processes 进程数组间的序号
     int      fd;// 通信的套接字句柄
} channel_t;

enum 
{
    CHANNEL_CMD_NONE = 0,
    CHANNEL_CMD_OPEN,
    CHANNEL_CMD_CLOSE,
    CHANNEL_CMD_QUIT,
    CHANNEL_CMD_TERMINATE,
    CHANNEL_CMD_REOPEN,
    CHANNEL_CMD_BACKUP,
};

int channel_write(int socket, channel_t *ch, size_t size);
int channel_read(int socket, channel_t *ch, size_t size);

void channel_close(int *fd);

#endif

