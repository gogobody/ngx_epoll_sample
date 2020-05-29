#include <stdio.h>

#include "dfs_lock.h"
#include "dn_time.h"

#define dfs_time_trylock(lock)  (*(lock) == 0 && CAS(lock, 0, 1))
#define dfs_time_unlock(lock)    *(lock) = 0
#define dfs_memory_barrier()    __asm__ volatile ("" ::: "memory") //内嵌汇编用法 memory barrier CPU越过内存屏障后，将刷新自己对存储器的缓冲状态
//memory强制gcc编译器假设RAM所有内存单元均被汇编指令修改，这样cpu中的registers和cache中已缓存的内存单元中的数据将作废。
// cpu将不得不在需要的时候重新读取内存中的数据。这就阻止了cpu又将registers，cache中的数据用于去优化指令，而避免去访问内存。
#define CACHE_TIME_SLOT    64
static uint32_t            slot; //
static uchar_t             dfs_cache_log_time[CACHE_TIME_SLOT]
                               [sizeof("1970/09/28 12:00:00")];

_xvolatile rb_msec_t       dfs_current_msec;
_xvolatile string_t        dfs_err_log_time;
_xvolatile struct timeval *dfs_time;
 struct timeval cur_tv;
_xvolatile uint64_t time_lock = 0;

int time_init(void)
{
    dfs_err_log_time.len = sizeof("1970/09/28 12:00:00.xxxx") - 1;
    
    dfs_time = &cur_tv;
    time_update();
	
    return NGX_OK;
}

void time_update(void)
{
    struct timeval  tv;
    struct tm       tm;
    time_t          sec;
    uint32_t        msec = 0;
    uchar_t        *p0 = nullptr;
    rb_msec_t       nmsec;

    if (!dfs_time_trylock(&time_lock)) 
	{
        return;
    }

    time_gettimeofday(&tv);
    sec = tv.tv_sec; // 秒
    msec = tv.tv_usec / 1000; //微秒/1000  毫秒
	
	dfs_current_msec = (rb_msec_t) sec * 1000 + msec;// 毫秒
	nmsec = dfs_time->tv_sec * 1000 + dfs_time->tv_usec/1000;  // dfs now time
	if ( dfs_current_msec - nmsec < 10) // 误差？小于10毫秒就不管
	{
		dfs_time_unlock(&time_lock);
		
		return;
    }
	/*
	 * 对于读写并发，nginx设计了NGX_TIME_SLOTS个slot，用于隔离读写操作的时间缓存。
	 * 同时引入时间缓存指针，原子地更新当前缓存的指向位置。
	 * */
	slot++;
	slot ^=(CACHE_TIME_SLOT - 1); // 异或，相当于做减法1^63 ->62
    dfs_time->tv_sec = tv.tv_sec;
	dfs_time->tv_usec = tv.tv_usec;
	
    time_localtime(sec, &tm);
    p0 = &dfs_cache_log_time[slot][0];
	sprintf((char*)p0, "%4d/%02d/%02d %02d:%02d:%02d.%04d",
        tm.tm_year, tm.tm_mon,
        tm.tm_mday, tm.tm_hour,
        tm.tm_min, tm.tm_sec, msec);
    dfs_memory_barrier(); // 内存屏障，这其中"memory"表示，告诉编译器内存的内容可能被更改了，需要无效所有Cache，并访问实际的内容，而不是Cache。
    
    dfs_err_log_time.data = p0;

    dfs_time_unlock(&time_lock);
}

_xvolatile string_t *time_logstr() //编译器不再对该变量的代码进行优化，不再从寄存器中读取变量的值，而是直接从它所在的内存中读取值
{
    return &dfs_err_log_time;
}

rb_msec_t time_curtime(void)
{
    return dfs_current_msec;
}

