#ifndef DFS_TASK_CMD_H
#define DFS_TASK_CMD_H

#include <cstdint>

#define OP_WRITE_BLOCK             80
#define OP_READ_BLOCK              81
#define OP_READ_METADATA           82
#define OP_REPLACE_BLOCK           83
#define OP_COPY_BLOCK              84
#define OP_BLOCK_CHECKSUM          85
#define OP_READ_BLOCK_ACCELERATOR  86
  
#define OP_STATUS_SUCCESS          0
#define OP_STATUS_ERROR            1  
#define OP_STATUS_ERROR_CHECKSUM   2 
#define OP_STATUS_ERROR_INVALID    3  
#define OP_STATUS_ERROR_EXISTS     4  
#define OP_STATUS_CHECKSUM_OK      5

typedef enum 
{
    NN_MKDIR = 1,
    NN_RMR,
    NN_LS,
    NN_GET_FILE_INFO,
    NN_CREATE,
    NN_GET_ADDITIONAL_BLK,
    NN_CLOSE,
    NN_RM,
    NN_OPEN,
    DN_REGISTER,
    DN_HEARTBEAT,
    DN_RECV_BLK_REPORT,
    DN_DEL_BLK,
    DN_DEL_BLK_REPORT,
    DN_BLK_REPORT,
    TASK_TEST

} cmd_t;

typedef enum
{
    SUCC = 0,
    FAIL = -10,
    MASTER_REDIRECT = 10,
    NO_MASTER = 101
} paxos_status;

typedef enum
{
    PERMISSION_DENY = -1,
    FSOBJECT_EXCEED = -2,
    NOT_DIRECTORY = -20,
    NOT_FILE = -21,
    IN_SAFE_MODE = -4,
    NOT_DATANODE
} opt_err;

typedef enum
{
    KEY_STATE_OK = 0,
    KEY_EXIST = -17,
    KEY_NOTEXIST = -2,
    KEY_STATE_CREATING
} fi_status;

typedef struct create_blk_info_s
{
	uint64_t blk_sz;
	short    blk_rep;
	// file list info
	int      blk_seq;
	int      total_blk;
} create_blk_info_t;

typedef struct create_resp_info_s
{
    uint64_t blk_id;
	uint64_t blk_sz;
	uint64_t namespace_id;
	short    dn_num;
	char     dn_ips[3][32];
	// add file list here
    int      blk_seq;
    int      total_blk;
} create_resp_info_t;

typedef struct report_blk_info_s
{
	uint64_t blk_id;
	uint64_t blk_sz;
	char     dn_ip[32];
} report_blk_info_t;

// 数据传输头
// datanode first get this header
typedef struct data_transfer_header_s
{
    int  op_type;  //option type 
	long namespace_id;
	long block_id;
	long generation_stamp;
	long start_offset;
	long len;
	//
	int blk_seq; // 当前切片
	int total_blk; // 总的切片
} data_transfer_header_t;

typedef struct data_transfer_header_rsp_s
{
    int op_status;
	int err;
} data_transfer_header_rsp_t;

#endif

