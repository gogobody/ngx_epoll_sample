#include "dfs_pipe.h"

int pipe_open(pipe_t *p)
{
    if (!p) 
	{
        return NGX_ERROR;
    }

    errno = 0;
    if (pipe(p->pfd)) 
	{
        return NGX_ERROR;
    }

    return NGX_OK;
}

void pipe_close(pipe_t *p)
{
    if (!p) 
	{
        return;
    }

    if (p->pfd[0] >= 0) 
	{
        close(p->pfd[0]);
    }

    if (p->pfd[1] >= 0) 
	{
        close(p->pfd[1]);
    }

    p->pfd[0] = NGX_INVALID_FILE;
    p->pfd[1] = NGX_INVALID_FILE;
    p->size = 0;
}

