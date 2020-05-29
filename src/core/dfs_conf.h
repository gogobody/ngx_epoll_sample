#ifndef DFS_CONF_H
#define DFS_CONF_H

#include "dfs_conf_base.h"
#include "dfs_types.h"
#include "dfs_string.h"
#include "dfs_array.h"
#include "dfs_memory_pool.h"
#include "dfs_error_log.h"

typedef struct conf_object_s    conf_object_t;
typedef struct conf_context_s   conf_context_t;
typedef struct conf_file_read_s conf_file_read_t;

struct conf_object_s 
{
    string_t       name;
    void *         (*init)(pool_t *pool);
    int            (*make_default)(void *);
    conf_option_t *option;  
};

struct conf_file_read_s 
{
    string_t  file_name;
    uchar_t  *buf; // file content
    int       fd;
    size_t    size;  
};

struct conf_context_s 
{
    conf_file_read_t  file; // conf file
    log_t            *log; //
    pool_t           *pool;
    array_t          *conf_objects_array; // 解析的配置文件 conf_variable_s
    int               conf_line;
    conf_object_t    *conf_objects; //配置文件 大类解析的相关操作，小类配置解析相关函数// nn_conf_objects in nn_conf.cpp
};

conf_context_t* conf_context_create(pool_t *pool);
int conf_context_init(conf_context_t*ctx, string_t *file, 
    log_t *log, conf_object_t* conf_objects);
int conf_context_parse(conf_context_t *ctx);
void *conf_get_parsed_obj(conf_context_t *ctx, string_t *name);

#endif

