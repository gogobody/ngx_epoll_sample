#include "dfs_task_codec.h"

// decode buffer to task
int task_decode(buffer_t *buff, task_t *task)
{
    int ret = 0;

    if (buffer_size(buff) <= 0) 
	{
		return NGX_AGAIN;
    }

    ret = task_decodefstr((char*)buff->pos, buffer_size(buff), task);
    if (ret <= 0) 
	{
        return ret;
    }
        
    buff->pos +=ret;

    return NGX_OK;
}

// encode task to buff
// buffer 装满或者装不下之后 返回 AGAIN
int task_encode(task_t *task, buffer_t *buff)
{
	int ret = 0;

	if (buffer_free_size(buff) <= 0) 
	{
		return NGX_AGAIN;
	}
		
	ret = task_encode2str(task, (char*)buff->last, buffer_free_size(buff));
	if (ret <= 0) 
	{
	    return ret;
	}
	        
	buff->last += ret;

	return NGX_OK;
}

