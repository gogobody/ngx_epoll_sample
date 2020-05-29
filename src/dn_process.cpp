#include <sys/ioctl.h>

#include "dn_process.h"
#include "dfs_error_log.h"
#include "dfs_conn.h"
#include "dfs_channel.h"
//#include "dn_signal.h"
#include "dn_module.h"
#include "dn_time.h"
#include "dn_conf.h"
//#include "dn_worker_process.h"
#include "dn_conn_event.h"
#include "dn_worker_process.h"

#define MASTER_TITLE "datanode: master process"

#define    MAX_TIMER 5

int        process_slot;             // process' slot
int        process_last;
pid_t      process_pid;
uint32_t   process_doing = 0;        // the action that process will do
uint32_t   process_type;
uint32_t   stop;

process_t  processes[PROCESSES_MAX]; // gloabal processes' info

static int      process_old_alived = NGX_FALSE;
dfs_thread_t   *main_thread = nullptr;

static int process_reap_workers(cycle_t *cycle);

int process_check_running(cycle_t *cycle)
{
    conf_server_t *sconf = nullptr;
    pid_t          pid;
    char          *pid_file = nullptr;
    struct stat    st;

    sconf = (conf_server_t *)dfs_cycle->sconf;

    pid_file = (char *)sconf->pid_file.data;

    if (stat(pid_file, &st) < 0)
	{
        return NGX_FALSE;
    }

    pid = process_get_pid(cycle);
    if (pid == (pid_t)NGX_ERROR)
	{
   	    return NGX_FALSE;
    }

    if (kill(pid, 0) < 0)
	{
   	    return NGX_FALSE;
    }

    return NGX_TRUE;
}

// 该函数在主线程循环中需要创建工作进程的地方被调用
// 用ngx_spawn_process函数创建工作进程，然后通知所有进程
static pid_t process_spawn(cycle_t *cycle, spawn_proc_pt proc, 
	                              void *data, char *name, int slot)
{
    pid_t     pid;
    uint64_t  on = 0;
    log_t    *log = nullptr;
    
    log = cycle->error_log;

    if (slot == PROCESS_SLOT_AUTO) //-1
	{
        for (slot = 0; slot < process_last; slot++) 
		{
            if (processes[slot].pid == NGX_INVALID_PID)  //先找到一个可用的slot
			{
                break;
            }
        }
		
        if (slot == PROCESSES_MAX)  //最多只能创建1024个子进程
		{
            dfs_log_error(log, DFS_LOG_WARN, 0,
                "no more than %d processes can be spawned", PROCESSES_MAX);
			
            return NGX_INVALID_PID;
        }
    }

    errno = 0;
	/* 
          这里相当于Master进程调用socketpair()为新的worker进程创建一对全双工的socket 
            
          实际上socketpair 函数跟pipe 函数是类似的，也只能在同个主机上具有亲缘关系的进程间通信，但pipe 创建的匿名管道是半双工的，
          而socketpair 可以认为是创建一个全双工的管道。
          int socketpair(int domain, int type, int protocol, int sv[2]);
          这个方法可以创建一对关联的套接字sv[2]。下面依次介绍它的4个参数：参数d表示域，在Linux下通常取值为AF UNIX；type取值为SOCK。
          STREAM或者SOCK。DGRAM，它表示在套接字上使用的是TCP还是UDP; protocol必须传递0；sv[2]是一个含有两个元素的整型数组，实际上就
          是两个套接字。当socketpair返回0时，sv[2]这两个套接字创建成功，否则socketpair返回一1表示失败。
             当socketpair执行成功时，sv[2]这两个套接字具备下列关系：向sv[0]套接字写入数据，将可以从sv[l]套接字中读取到刚写入的数据；
          同样，向sv[l]套接字写入数据，也可以从sv[0]中读取到写入的数据。通常，在父、子进程通信前，会先调用socketpair方法创建这样一组
          套接字，在调用fork方法创建出子进程后，将会在父进程中关闭sv[l]套接字，仅使用sv[0]套接字用于向子进程发送数据以及接收子进程发
          送来的数据：而在子进程中则关闭sv[0]套接字，仅使用sv[l]套接字既可以接收父进程发来的数据，也可以向父进程发送数据。
          注意socketpair的协议族为AF_UNIX UNXI域
          */  
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, processes[slot].channel) == NGX_ERROR)
    {
        return NGX_INVALID_PID;
    }
	
	/* 设置master的channel[0](即写端口)，channel[1](即读端口)均为非阻塞方式 */  
    if (conn_nonblocking(processes[slot].channel[0]) == NGX_ERROR)
	{
        channel_close(processes[slot].channel);

        return NGX_INVALID_PID;
    }

    if (conn_nonblocking(processes[slot].channel[1]) == NGX_ERROR)
	{
        channel_close(processes[slot].channel);

        return NGX_INVALID_PID;
    }
	
	/* 
			 设置异步模式： 这里可以看下《网络编程卷一》的ioctl函数和fcntl函数 or 网上查询 
		   */ 

    on = 1;// 标记位，ioctl用于清除（0）或设置（非0）操作  
    
	/* 
          设置channel[0]的信号驱动异步I/O标志 
          FIOASYNC：该状态标志决定是否收取针对socket的异步I/O信号（SIGIO） 
          其与O_ASYNC文件状态标志等效，可通过fcntl的F_SETFL命令设置or清除 
         */ 

    if (ioctl(processes[slot].channel[0], FIOASYNC, &on) == NGX_ERROR)
	{
        channel_close(processes[slot].channel);

        return NGX_INVALID_PID;
    }
	/* F_SETOWN：用于指定接收SIGIO和SIGURG信号的socket属主（进程ID或进程组ID） 
			  * 这里意思是指定Master进程接收SIGIO和SIGURG信号 
			  * SIGIO信号必须是在socket设置为信号驱动异步I/O才能产生，即上一步操作 
			  * SIGURG信号是在新的带外数据到达socket时产生的 
			 */ 

    if (fcntl(processes[slot].channel[0], F_SETOWN, process_pid) == NGX_ERROR)
	{
        return NGX_INVALID_PID;
    }

    if (fcntl(processes[slot].channel[0], F_SETFD, FD_CLOEXEC) == NGX_ERROR)
	{
        channel_close(processes[slot].channel);

        return NGX_INVALID_PID;
    }

    if (fcntl(processes[slot].channel[1], F_SETFD, FD_CLOEXEC) == NGX_ERROR)
	{
        channel_close(processes[slot].channel);

        return NGX_INVALID_PID;
    }

    process_slot = slot; // 这一步将在ngx_pass_open_channel()中用到，就是设置下标，用于寻找本次创建的子进程  

    pid = fork();
    // 这里是第三个进程了
    printf("pid:%d\n",pid);
    switch (pid) 
	{
        case NGX_INVALID_PID:

            channel_close(processes[slot].channel);

            return NGX_INVALID_PID;
			
        case 0: //子进程
            puts("子进程run");
            process_pid = getpid();  // 设置子进程ID 
            
            proc(cycle, data); // 调用proc回调函数，即ngx_worker_process_cycle。之后worker子进程从这里开始执行

            return NGX_INVALID_PID;
			
        default: //父进程，但是这时候打印的pid为子进程ID

            break;
    }
    puts("main thread first out log");
    processes[slot].pid = pid;
    processes[slot].proc = proc;
    processes[slot].data = data;
    processes[slot].name = name;
    processes[slot].ps = PROCESS_STATUS_RUNNING;
    processes[slot].ow  = NGX_FALSE;
    processes[slot].restart_gap = dfs_time->tv_sec;

    if (slot == process_last) 
	{
        process_last++;
    }

    return pid;
}


void process_broadcast(int slot, int cmd)
{
    int       s;
    channel_t ch;

    ch.command = cmd;
    ch.pid = processes[slot].pid;
    ch.slot = slot;
    ch.fd = processes[slot].channel[0];

    for (s = 0; s < process_last; s++) 
	{
        if ((s == process_slot)
            || (processes[s].ps & PROCESS_STATUS_EXITED)
            || (processes[s].ps & PROCESS_STATUS_EXITING)
            || (processes[s].pid == NGX_INVALID_PID)
            || (processes[s].channel[0] == NGX_INVALID_FILE)
            || (processes[s].ow == NGX_TRUE))
        {
            continue;
        }

		// broadcast the new process's channel to all other processes
        channel_write(processes[s].channel[0], &ch, sizeof(channel_t));
    }
}

void process_set_title(string_t *title)
{
    // copy the title to argv[0], and set argv[1] to nullptr
//    string_strncpy(dfs_argv[0], title->data, title->len);
//
//    dfs_argv[0][title->len] = 0;
//    dfs_argv[1] = nullptr;
}

void process_get_status()
{
    int   i = 0;
    int   status = 0;
    pid_t pid;

    for ( ;; ) 
	{
        pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) 
		{
            return;
        }

        if (pid == NGX_INVALID_PID)
		{

            if (errno == DFS_EINTR) 
			{
                continue;
            }

            return;
        }

        for (i = 0; i < process_last; i++) 
		{
            if (processes[i].pid == pid) 
			{
                processes[i].status = status;
                processes[i].ps = PROCESS_STATUS_EXITED;

                break;
            }
        }

        processes[i].status = status;
        processes[i].ps = PROCESS_STATUS_EXITED;
    }
}

int process_change_workdir(string_t *dir)
{
    if (chdir((char*)dir->data) < 0) 
	{
        dfs_log_error(dfs_cycle->error_log, DFS_LOG_FATAL, errno,
            "process_change_workdir failed!\n");

        return NGX_ERROR;
    }

    return NGX_OK;
}

void process_set_old_workers()
{
    int i = 0;

    for (i = 0; i < process_last; i++) 
	{
        if (processes[i].pid != NGX_INVALID_PID
            && (processes[i].ps & PROCESS_STATUS_RUNNING)) 
        {
            processes[i].ow = NGX_TRUE;
        }
    }
}

void process_signal_workers(int signo)
{
    int       i = 0;
    channel_t ch;

    switch (signo) 
	{

			
        default:
            ch.command = CHANNEL_CMD_NONE;
            break;
    }

    ch.fd = NGX_INVALID_FILE;

    /* 子进程创建的时候，父进程的东西都会被子进程继承，
     * 所以后面创建的进程能够得到前面进程的channel信息，
     * 直接可以和他们通信，那么前面创建的进程如何知道后面的进程信息呢？
     * 很简单，既然前面创建的进程能够接受消息，那么我就发个信息告诉他后面的进程的channel,并把信息保存在channel[0]中，
     * 这样就可以相互通信了。*/
    for (i = 0; i < process_last; i++) 
	{
        if (processes[i].pid == NGX_INVALID_PID
            || processes[i].ow == NGX_FALSE)
        {
            continue;
        }

        if (ch.command != CHANNEL_CMD_NONE) 
		{
            //向 每个进程 channel[0]发送信息
            if (channel_write(processes[i].channel[0], &ch,
                              sizeof(channel_t)) == NGX_OK)
            {
                processes[i].ps |= PROCESS_STATUS_EXITING;
                processes[i].ow = NGX_FALSE;
				
                continue;
            }
        }

        dfs_log_debug(dfs_cycle->error_log, DFS_LOG_DEBUG, 0,
            "kill (%P, %d)", processes[i].pid, signo);
		
        if (kill(processes[i].pid, signo) == NGX_ERROR)
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
                "kill(%P, %d) failed", processes[i].pid, signo);
        }
		
        processes[i].pid = NGX_INVALID_PID;
        channel_close(processes[i].channel);
        processes[i].channel[0] = NGX_INVALID_FILE;
        processes[i].channel[1] = NGX_INVALID_FILE;
        processes[i].ps = 0;
        processes[i].ow = NGX_FALSE;
    }
}

void process_notify_workers_backup()
{
    int       idx = 0;
    channel_t ch;
	
    ch.fd = NGX_INVALID_FILE;
    ch.command = CHANNEL_CMD_BACKUP;

    for (idx = 0; idx < process_last; idx++) 
	{
        if (processes[idx].pid == NGX_INVALID_PID)
		{
            continue;
        }

        if (channel_write(processes[idx].channel[0], &ch,
                          sizeof(channel_t)) != NGX_OK)
        {
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_WARN,
                    0, "send backup command error!");
        }
    }

    return;
}


// process_spawn 上面的进程生成？ worker_processer //from dn_worker_process.c
int  process_start_workers(cycle_t *cycle)
{
    dfs_log_debug(cycle->error_log, DFS_LOG_DEBUG, 0, "process_start_workers");

    if (process_spawn(cycle, worker_processer, nullptr,
        (char *)"worker process", PROCESS_SLOT_AUTO) == NGX_INVALID_PID)
    {
        return NGX_ERROR;
    }

    process_broadcast(process_slot, CHANNEL_CMD_OPEN);

    return NGX_OK;
}

// 开启监听端口并start worker ，执行上面的 startworkers
void  process_master_cycle(cycle_t *cycle, int argc, char **argv)
{
    int       i = 0;
    int       live = 1;

    process_type = PROCESS_MASTER;

	// thread  多线程私有数据pthread_key_create
	thread_env_init();
	
	main_thread = thread_new(nullptr);
    if (!main_thread) 
	{
        return;
    }
	
    main_thread->type = THREAD_MASTER;
    thread_bind_key(main_thread); // 将master thread 作为线程共享
	

    memset(processes, 0x00, sizeof(processes));

    for (i = 0; i < PROCESSES_MAX; i++) 
	{
        processes[i].pid = NGX_INVALID_PID;
    }

	// 本质上这是一个跨进程的互斥锁，以这个互斥锁来保证只有一个进程具备监听accept事件的能力
	accept_lock_init();
    // 开启监听端口 listening fd = sockfd
    // listen_rev_handler 处理 listening 事件
    // cli 的listening初始化，并加入listening数组

    // todo:
    if (conn_listening_init(cycle) != NGX_OK)
	{
        return;
    }
    // start worker
    if (process_start_workers(cycle) != NGX_OK)
	{
        return;
    }

    // 由于前面已经对set进行了清空，因而这里重新监听这些信号，以对这些信号进行处理，这里需要注意的是，
    // 如果没有收到任何信号，主进程就会被挂起在这个位置。
    // 关于master进程处理信号的流程，这里需要说明的是，在nginx.c的main()方法中，会调用
    // ngx_init_signals()方法将全局变量signals中定义的信号及其会调用方法设置到当前进程的信号集中。
    // 而signals中定义的信号的回调方法都是ngx_signal_handler()方法，
    // 该方法在接收到对应的信号之后，会设置对应的标志位，也即下面多个if判断中的参数，
    // 通过这种方式来触发相应的逻辑的执行。


    // master thread
    for ( ;; ) 
	{
        dfs_log_debug(cycle->error_log, DFS_LOG_DEBUG, 0, "sigsuspend");

        time_update();

        while (process_doing) 
		{
            if (!stop && process_doing & PROCESS_DOING_REAP) 
			{
                process_doing &= ~PROCESS_DOING_REAP;

                process_get_status();

                dfs_log_debug(cycle->error_log, DFS_LOG_DEBUG, 0, 
					"reap children");
                // 有子进程意外结束，这时需要监控所有子进程
                /*ngx_reap_children(会遍历ngx_process数组，检查每个子进程的状态，对于非正常退出的子进程会重新拉起，
			最后，返回一个live标志位，如果所有的子进程都已经正常退出，则live为0，初次之外live为1。*/
                live = process_reap_workers(cycle);

                process_old_alived = NGX_FALSE;
				
                continue;
            }

            if (!live && ((process_doing & PROCESS_DOING_QUIT) 
                || (process_doing & PROCESS_DOING_TERMINATE))) 
            {
                return;
            }


            if (process_doing & PROCESS_DOING_BACKUP && live == NGX_TRUE)
			{
                process_notify_workers_backup();
                process_doing &= ~PROCESS_DOING_BACKUP;
            }
        }
    }
	
    exit(-1);
}

static int process_reap_workers(cycle_t *cycle)
{
    int i = 0;
    int live = NGX_FALSE;
	
    for (i = 0; i < process_last; i++) 
	{
        if (processes[i].pid == NGX_INVALID_PID)
		{
            continue;
        }

        if (!(processes[i].ps & PROCESS_STATUS_EXITED)) 
		{
            live = NGX_TRUE;
			
            if (processes[i].ow == NGX_TRUE)
			{
                process_old_alived = NGX_TRUE;
            }
			
            continue;
        }
		
        if (processes[i].ow == NGX_TRUE)
		{
            processes[i].pid = NGX_INVALID_PID;
            processes[i].ow = 0;
            channel_close(processes[i].channel);
            processes[i].channel[0] = NGX_INVALID_FILE;
            processes[i].channel[1] = NGX_INVALID_FILE;
            continue;
        }
		
        // detach this process
        processes[i].pid = NGX_INVALID_PID;
        processes[i].ps = PROCESS_STATUS_EXITED;
        channel_close(processes[i].channel);
        processes[i].channel[0] = NGX_INVALID_FILE;
        processes[i].channel[1] = NGX_INVALID_FILE;
        // respawn the process if need

        if (dfs_time->tv_sec - processes[i].restart_gap <= MAX_RESTART_NUM) 
		{
            dfs_log_error(cycle->error_log, DFS_LOG_ALERT, 0,
                "over max restart num %s", processes[i].name);
			
            continue;
        }
		
        if (process_spawn(cycle, processes[i].proc,
            processes[i].data, processes[i].name, i)
            == NGX_INVALID_PID)
        {
            dfs_log_error(cycle->error_log, DFS_LOG_ALERT, 0,
                "can not respawn %s", processes[i].name);
			
            continue;
        }

        live = NGX_TRUE;
    }

    return live;
}

int process_write_pid_file(pid_t pid)
{
    int            fd;
    ssize_t        n = 0;
    uchar_t        buf[10] = {0};
    uchar_t       *last = 0;
    uchar_t       *pid_file = nullptr;
    conf_server_t *sconf = nullptr;

    sconf = (conf_server_t *)dfs_cycle->sconf; 
    if (!sconf) 
	{
        return NGX_ERROR;
    }
	
    pid_file = sconf->pid_file.data;
    last = string_xxsnprintf(buf, 10, "%d", pid); // ?
	
    fd = dfs_sys_open(pid_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) 
	{
        dfs_log_error(dfs_cycle->error_log, DFS_LOG_WARN, 0,
                "process_write_pid_file failed!");
		fprintf(stderr,"%s\n",strerror(errno));
        return NGX_ERROR;
    }

    n = write(fd, buf, last - buf);

    close(fd);

    if (n < 0) 
	{
        return NGX_ERROR;
    }

    return NGX_OK;
}

//get pid from pid file?
int process_get_pid(cycle_t *cycle)
{
    int            n = 0;
    int            fd;
    char           buf[11] = {0};
    uchar_t       *pid_file = nullptr;
    conf_server_t *sconf  = (conf_server_t *)dfs_cycle->sconf;

    pid_file = sconf->pid_file.data;

    fd = dfs_sys_open(pid_file, O_RDWR , 0644);
    if (fd < 0) 
	{
        dfs_log_error(cycle->error_log,DFS_LOG_WARN, errno,
            "process_write_pid_file failed!");
		
        return NGX_ERROR;
    }

    n = read(fd, buf, 10);

    close(fd);

    if (n <= 0) 
	{
        return NGX_ERROR;
    }

    return atoi(buf);
}

void process_del_pid_file(void)
{
    uchar_t       *pid_file = nullptr;
    conf_server_t *sconf = nullptr;
    
    sconf = (conf_server_t *)dfs_cycle->sconf;
    pid_file = sconf->pid_file.data;
    
    unlink((char *)pid_file);
}

void process_close_other_channel()
{
    int i = 0;
	
    for (i = 0; i < process_last; i++) 
	{
        if (processes[i].pid == NGX_INVALID_PID)
		{
            continue;
        }
		
        if (i == process_slot) 
		{
            continue;
        }
		
        if (processes[i].channel[1] == NGX_INVALID_FILE)
		{
            continue;
        }
		
        if (close(processes[i].channel[1]) == NGX_ERROR)
		{
            dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
                "close() channel failed");
        }
    }

    // close this process's write fd
    if (close(processes[process_slot].channel[0]) == NGX_ERROR)
	{
        dfs_log_error(dfs_cycle->error_log, DFS_LOG_ALERT, errno,
            "close() channel failed");
    }
}

int process_quit_check()
{
    return (process_doing & PROCESS_DOING_QUIT) ||
        (process_doing & PROCESS_DOING_TERMINATE);
}

int process_run_check()
{
    return (!(process_doing & PROCESS_DOING_QUIT))
        && (!(process_doing & PROCESS_DOING_TERMINATE));
}

// 对于父进程而言，他知道所有进程的channel[0]， 直接可以向子进程发送命令
process_t * get_process(int slot)
{
    return &processes[slot];
}

void process_set_doing(int flag)
{
    process_doing |= flag;
}

int process_get_curslot() 
{
    return process_slot;
}

