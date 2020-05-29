#ifndef DN_CONF_H
#define DN_CONF_H

#include "dfs_types.h"
#include "dfs_array.h"
#include "dfs_conf.h"

typedef struct conf_server_s conf_server_t;

// cycle -> sconf
struct conf_server_s 
{
    int      daemon;
    int      worker_n;
    array_t  bind_for_cli;
	string_t ns_srv;
	string_t listen_for_other_dn; // used for other dn
    string_t my_paxos;  // dn's own paxos
    string_t ot_paxos;  // paxos's NodeList
    uint32_t connection_n; // 连接
    string_t error_log;
    string_t coredump_dir;
    string_t pid_file;
    uint32_t log_level;
    uint32_t recv_buff_len;
    uint32_t send_buff_len;
    uint32_t max_tqueue_len;
    string_t data_dir;
	uint32_t heartbeat_interval;
	uint32_t block_report_interval;
};

conf_object_t *get_dn_conf_object(void);

#define DEF_RBUFF_LEN          64 * 1024
#define DEF_SBUFF_LEN          64 * 1024
#define DEF_MMAX_TQUEUE_LEN    1000

#define set_def_string(key, value) do { \
    if (!(key)->len) { \
        (key)->data = (uchar_t *)(value); \
        (key)->len = sizeof(value); \
    }\
} while (0)

#define set_def_int(key, value) do { \
    if ((key) == CONF_SIZE_NOT_SET) { \
        (key) = value; \
    } \
} while (0)

#define set_def_time(key, value) do { \
    if ((key) == CONF_TIME_T_NOT_SET) { \
        (key) = value;\
    }\
} while (0)

#endif

