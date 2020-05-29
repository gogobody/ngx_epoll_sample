#include "dfs_conf.h"
#include "dfs_memory.h"

#define CONF_MAX_CLASS_N      32
#define CONF_MAX_OPTION_S     1024
#define CONF_MAX_OPTION_N     8
#define CONF_MAX_TYPE_ARRAY_N 256
#define CONF_MAX_OPT_ARRAY_N  1024
#define CONF_POOL_SIZE        16384

typedef struct conf_args_s 
{
	array_t *arr;
	int      line;
} conf_args_t;

static int conf_file_read(conf_file_read_t *file, log_t *log);
static int conf_parse_buf(conf_context_t *ctx, uchar_t *buf, size_t size);
static int conf_file_close(conf_file_read_t *file, log_t *log);
static int conf_make_objects_default(conf_context_t *ctx);
static int conf_parse_type(conf_context_t *ctx, conf_args_t *conf_args);
static int conf_parse_option(conf_context_t *ctx, conf_args_t *conf_args);
static conf_variable_t * conf_get_vobject(conf_context_t *ctx, 
	string_t *name);
static int conf_parse_object(conf_context_t *ctx, string_t *var, 
	conf_object_t *obj);
static int conf_parse_object_att(conf_context_t *ctx, conf_variable_t *v, 
    string_t *att, string_t *args, int args_n, conf_option_t *option);
static int conf_string_pdup(pool_t *pool, string_t *dst, string_t *src);


//从pool为conf_ctx分配空间
// 配置文件上下文
conf_context_t * conf_context_create(pool_t *pool)
{
    conf_context_t *ctx = nullptr;
	
    ctx = (conf_context_t *)pool_calloc(pool, sizeof(conf_context_t));
    if (!ctx) 
	{
        return ctx;
    }
	
    ctx->pool = pool;
    return ctx;
}


int conf_context_init(conf_context_t*ctx, string_t *file, log_t *log, 
	                       conf_object_t* conf_objects)
{
    ctx->log = log;
    ctx->conf_objects = conf_objects;
	
    return conf_string_pdup(ctx->pool, &ctx->file.file_name, file);
}
// parse conf file
int conf_context_parse(conf_context_t *ctx)
{
    int               ret;
    struct stat       s{};
    string_t         *name = nullptr;
    conf_file_read_t *file = nullptr;
    
    errno = 0;
    file = &ctx->file;
    name = &file->file_name;
	
    file->fd = dfs_sys_open(name->data, O_RDONLY, 0);
    if (file->fd < 0) 
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, errno,
            "open config file \"%V\" failed",  name);
		
        return NGX_ERROR;
    }

    ret = fstat(file->fd, &s);
    if (ret != NGX_OK)
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, errno,
            "fstat file fd:%d, \"%V\" failed", name);
		
        conf_file_close(file, ctx->log);
		
        return NGX_ERROR;
    }

    file->size = s.st_size;

	// mmap 把文件读取到 file -> buf
    if (conf_file_read(file, ctx->log) != NGX_OK)
	{
        conf_file_close(file, ctx->log);
		
        return NGX_ERROR;
    }

    ctx->conf_objects_array = (array_t *)array_create(ctx->pool, CONF_MAX_CLASS_N,
        sizeof(conf_variable_t));

    if (ctx->conf_objects_array == nullptr)
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0, 
			"create variable table failed");
		
        conf_file_close(file, ctx->log);
		
        return NGX_ERROR;
    }

	// 解析上面保存的用户空间的文件 ngx_conf_read_token
    if (conf_parse_buf(ctx, file->buf, file->size) != NGX_OK)
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0, 
            "parse bfuf failed conf_line:%d", ctx->conf_line);
		
        conf_file_close(file, ctx->log);
		
        return NGX_ERROR;
    }

    conf_file_close(file, ctx->log);

    // make default
    if (conf_make_objects_default(ctx) != NGX_OK)
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0, 
            "conf_make_objects_default failed conf_line");
		
        return NGX_ERROR;
    }

    return NGX_OK;
}

//参数fd为即将映射到进程空间的文件描述字
static int conf_file_read(conf_file_read_t *file, log_t *log)
{
    file->buf = (uchar_t *)mmap(nullptr, file->size, PROT_READ,MAP_PRIVATE, file->fd, 0);//将文件映射到用户空间
    if (!file->buf) 
		{
        dfs_log_error(log, DFS_LOG_ERROR, errno,
            "conf_file_read: mmap config file failed");
		
        return NGX_ERROR;
    }

    return NGX_OK;
}
// 解析上面保存的用户空间的文件 gx_conf_read_token
// https://blog.csdn.net/jackywgw/article/details/51454006?xx

static int conf_parse_buf(conf_context_t *ctx, uchar_t *buf, size_t size)
{
    int           comment = 0; // 注释
    int           line = 1;
    int           first_quote = 0;
    int           last_quote = 0;
    uchar_t      *last_pos;
    size_t        option_len = 0;
    pool_t       *tmp_pool = nullptr;
    uchar_t      *last = nullptr;
    uchar_t      *end = nullptr;
    uchar_t       ch = 0;
    uchar_t      *q = nullptr; // normal char
    uchar_t      *option = nullptr;
    array_t      *args = nullptr; // 多个数组空间
    string_t     *word = nullptr;
    uchar_t      *p = nullptr;
    array_t      *arr_opts = nullptr;
    array_t      *arr_type = nullptr;
    conf_args_t  *opt = nullptr;  //先把配置文件解析存在 conf_args数组里
    conf_args_t  *type = nullptr; 
    int           type_n = 0;
    int           opt_n = 0;
    int           i = 0;

    if (!buf || size == 0) 
	{
        return NGX_ERROR;
    }

    tmp_pool = pool_create(CONF_POOL_SIZE, DFS_PAGE_SIZE, ctx->log);
    if (!tmp_pool) 
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0, "no space to alloc");
		
        return NGX_ERROR;
    }

    // arr_type 存大类 // Server server;
    arr_type = (array_t *)array_create(tmp_pool, CONF_MAX_TYPE_ARRAY_N, sizeof(conf_args_t));
    // arr_opts 存大类里的小类，保存 多个数组，每个数组保存一行解析的配置文件，比如：“server.daemon","=","ALLOW”
    arr_opts = (array_t *)array_create(tmp_pool, CONF_MAX_TYPE_ARRAY_N, sizeof(conf_args_t));

    // 创建 string_t 的数组
    args = (array_t *)array_create(tmp_pool, CONF_MAX_OPTION_N, sizeof(string_t));
    option = (uchar_t *)pool_alloc(tmp_pool, CONF_MAX_OPTION_S + 1);

    q = option;

    p = buf; // position
    end = buf + size;

    for ( ; p < end; ) 
	{
        ch = *p;
        // 遇到注释 comment is starting by "#"
        if (comment && ch != '\n') 
		{
            p++;
			
            continue;
        }
        // 回车
        if (ch == '\r'){
            p++;

            continue;
        }
        
        switch(ch) 
		{
            case ' ':
                if (first_quote && !last_quote) 
				{
                    goto normal_char;
                }
				
                if (option_len) 
				{
                    // get one option, then save it
                    word = (string_t *)array_push(args);
                    if (word == nullptr) 
					{
                        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                            "at line %d: no more space to parse configure", line);
                        ctx->conf_line = line;
						
                        return NGX_ERROR;
                    }
					
                    word->data = (uchar_t *)pool_alloc(tmp_pool, option_len + 1);
                    if (!word->data) 
					{
                        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                            "at line %d: no more space to parse configure", line);
                        ctx->conf_line = line;
						
                        return NGX_ERROR;
                    }
					
                    word->len = option_len;
                    last = memory_cpymem(word->data, option, option_len);
                    *last = 0;
                }
				
                // reset, and get the next option
                if (*option != 0) 
				{
                    memory_zero(option, CONF_MAX_OPTION_S + 1);
                }
				
                q = option;
                option_len = 0;
                last_quote = 0;
                first_quote = 0;
				
                break;
				
            case '\\':
                p++;
                ch = *p;
                switch (ch) 
				{
                    case '\n':
                        p++;
                        continue;
						
                    case '"':
                    case '\'':
                        *q++ = *p;
                        option_len++;
                        break;
						
                    default:
                        *q++ = '\\';
                        *q++ = *p;
                        option_len += 2;
                        break;
                }
				
                break;

            case '"':
                if (!first_quote) 
				{
                    // miss the right part of '' ''
                    first_quote = 1;
                } 
				else 
				{
                    last_quote = 1;
                    last_pos = p + 1;
                    if ((last_pos == end) 
						|| (*last_pos != ';' && *last_pos != ' ')) 
                    {
                        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                            "at line %d: invalid char after last quote", line);
						
                        ctx->conf_line = line;
						
                        return NGX_ERROR;
                    }
                }
				
                break;
				
            case '#': // 说明是注释
                if (first_quote && !last_quote) 
				{
                    goto normal_char;
                }
				
                if (option_len) 
				{
                    dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                        "at line %d: '#' should be in quotes if not comments", line);
					
                    ctx->conf_line = line;
					
                    return NGX_ERROR;
                }
				
                comment = 1;
                break;
				
            case '\n':
                if (first_quote && !last_quote) 
				{
                    dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                        "at line %d: not allow '\\n' appeared in quotes ", line);
					
                    ctx->conf_line = line;
					
                    return NGX_ERROR;
                }

                line++;
                first_quote = 0;
                last_quote = 0;
                if (p + 1 == end && (*(p - 1) != ';') && !comment) 
				{
                    dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                        "missing ';' in file's EOF at line %d", line);
					
                    ctx->conf_line = line;
					
                    return NGX_ERROR;
                }
				
                if (comment) 
				{
                    comment = 0;
                }
				
                break;
				
            case ';':
                if (first_quote && !last_quote) 
				{
                    goto normal_char;
                }
				
                if (option_len) 
				{
                    word = (string_t *)array_push(args);
                    word->data = (uchar_t *)pool_alloc(tmp_pool, option_len + 1);
                    if (!word->data) 
					{
                         dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                            "at line %d: no more space to parse configure", line);
						 
                         ctx->conf_line = line;
						 
                         return NGX_ERROR;
                    }
					
                    word->len = option_len;
                    last = memory_cpymem(word->data, option, option_len);
                    *last = 0;
                } 
				else 
				{
                    word = (string_t *)array_push(args);
                    word->data = nullptr;
                    word->len = 0;
                }

                memory_zero(option, CONF_MAX_OPTION_S + 1);
                q = option;
                option_len = 0;

                if (args->nelts >= 3) 
				{
                    opt = (conf_args_t *)array_push(arr_opts);
                    opt->arr = args;
                    opt->line = line;
                } 
				else if (args->nelts == 2) // Server server;
				{
                    type = (conf_args_t *)array_push(arr_type);
                    type->arr = args; // 直接保存配置的数组
                    type->line = line;
                } 
				else 
				{
                    if (args->nelts == 1) 
					{
                        if (word->len) 
						{
                            dfs_log_debug(ctx->log, DFS_LOG_DEBUG, 0,
                                "at line %d: invalid directive \"%V\", "
                                "here need more parameters", line, word);
                        } 
						else 
						{
                            dfs_log_debug(ctx->log, DFS_LOG_DEBUG, 0,
                                "at line %d: duplicate semicolon");
                        }
                    }

                    ctx->conf_line = line;
					
                    goto error;
                }
				
                args = (array_t *)array_create(tmp_pool, CONF_MAX_OPTION_N,
                    sizeof(string_t));
                first_quote = 0;
                last_quote = 0;
                break;
				
            default:
normal_char:
                *q++ = *p; 
                option_len++;
                break;
        }
		
        p++;
    }

    //first parse types
    if (first_quote && !last_quote) 
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
            "missing last quote when parse complete at line %d of \"%V\"",
        line, &ctx->file.file_name);
		
        ctx->conf_line = line;
		
        goto error;
    }

    // 解析完字符串的二维配置文件数组
    // 大类
    type = (conf_args_t *)arr_type->elts;
    type_n = arr_type->nelts;
    for (i = 0; i < type_n; i++) 
	{
        ctx->conf_line = type[i].line;// 行数
		// 解析到上下文 Server server
        if (conf_parse_type(ctx, &type[i]) != NGX_OK)
		{
            goto error;
        }
    }

    // 解析小类
    opt = (conf_args_t *)arr_opts->elts;
    opt_n = arr_opts->nelts;
	
    for (i = 0; i < opt_n; i++) 
	{
        ctx->conf_line = type[i].line;
        // 解析小类
        if (conf_parse_option(ctx, &opt[i]) != NGX_OK)
		{
            goto error;
        }
    }

    pool_destroy(tmp_pool);

    return NGX_OK;

error:

    pool_destroy(tmp_pool);

    return NGX_ERROR;
}

static int conf_file_close(conf_file_read_t *file, log_t *log)
{
    if (file->fd < 0) 
	{
        return NGX_OK;
    }

    if (!file->buf) 
	{
         dfs_log_error(log, DFS_LOG_ERROR, 0,
            "conf_file_close: close file failed");
		 
        return NGX_ERROR;
    }

    munmap(file->buf, file->size);
    close(file->fd);
    file->buf = nullptr;
    file->fd = -1;
    file->size = 0;

    return NGX_OK;
}

static int conf_make_objects_default(conf_context_t *ctx)
{
    uint32_t         i = 0;
    array_t         *conf_objects_array = nullptr;
    conf_variable_t *v = nullptr;
 
    conf_objects_array = ctx->conf_objects_array;
    v = (conf_variable_t *)conf_objects_array->elts;

    for (i = 0; i < conf_objects_array->nelts; i++) 
	{
        if (v[i].make_default != nullptr && v[i].make_default(&v[i]) != NGX_OK)
		{
            dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
                "%V make_default fail", &v->name);
			
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

// 解析配置文件数组 conf_args<string_t> Server server
static int conf_parse_type(conf_context_t *ctx, conf_args_t *conf_args)
{
    int            i = 0;
    string_t      *word = nullptr;
    conf_object_t *objects = nullptr;
    array_t       *args = nullptr;
    log_t         *log = nullptr;
	
    log = ctx->log;
    args = conf_args->arr;
    word = (string_t *)args->elts;
    objects = ctx->conf_objects; // in this application ,there is only one case "Server"
	
    if (args->nelts > 0 && args->nelts != 2) 
	{
        dfs_log_error(log, DFS_LOG_ERROR, 0, "at line %d: unknown directive: \"%V\""
            "it might miss parameters",ctx->conf_line, &word[0]);
    }
	
    for (i = 0; objects[i].name.len > 0; i++ ) 
	{
        if (word[0].len != objects[i].name.len) 
		{
            continue;
        }
		
        if (string_xxstrncasecmp(word[0].data, objects[i].name.data,
            word[0].len)) 
        {
            continue;
        }

        // Server server
        if (conf_parse_object(ctx, &word[1], &objects[i]) != NGX_OK)
		{
            dfs_log_error(log, DFS_LOG_ERROR, 0, 
                "at line %d: unknown object: \"%V\"", ctx->conf_line, &word[1]);
			
            return NGX_ERROR;
        }
		
        args->nelts = 0;
		
        return NGX_OK;
    }

    dfs_log_error(log, DFS_LOG_ERROR, 0, "at line %d: unknown directive: "
        "\"%V %V;\"", ctx->conf_line, &word[0], &word[1]);

    return NGX_ERROR;
}

// 解析一行的 option
static int conf_parse_option(conf_context_t *ctx, conf_args_t *conf_args)
{
    int              i = 0;
    uchar_t         *ch = nullptr;
    string_t        *word = nullptr;
    string_t         var = string_null;
    string_t         att = string_null;
    conf_object_t   *objects = nullptr;
    conf_variable_t *vclass = nullptr;
    int              line = 0;
    array_t         *args = nullptr;
    log_t           *log = nullptr;
	
    log = ctx->log;
    args = conf_args->arr;
    word = (string_t *)args->elts;
    line = conf_args->line;
    objects = ctx->conf_objects;
  
    // check args number
    if (args->nelts > CONF_TAKE12 || args->nelts < CONF_TAKE2) 
	{
        dfs_log_error(log, DFS_LOG_ERROR, 0, "at line %d: invalid parameters, " 
            "only allows from two to four", line);
		
        return NGX_ERROR;
    }

    // word[1] is "=" or "~"
    if (args->nelts == CONF_TAKE2) 
	{
        for ( i = 0; objects[i].name.len > 0; i++ ) 
		{
            if (word[0].len != objects[i].name.len) 
			{
                continue;
            }
			
            if (string_xxstrncasecmp(word[0].data, objects[i].name.data,
                word[0].len))
            {
                continue;
            }

            // 在这里调用了 init 函数
            if (conf_parse_object(ctx, &word[1], &objects[i]) != NGX_OK)
			{
                return NGX_ERROR;
            }
			
            return NGX_OK;
        }

        dfs_log_error(log, DFS_LOG_ERROR, 0, "at line %d: Unknown directive \"%V\"", 
                line, &word[0]);
		
        return NGX_ERROR;
    } 
	else 
	{
        ch = (uchar_t *)string_strchr(word[0].data, '.'); // eg: .daemon
        if (!ch) 
		{
            dfs_log_error(log , DFS_LOG_ERROR, 0, "at line %d: missing \".\" in"
                "\"%V\"", line, &word[0]);
			
            return NGX_ERROR;
        }
		
        var.data = word[0].data ;
        var.len = ch - var.data;
        att.data = ch + 1;
        att.len = word[0].len - var.len - 1;
        // lookup for 'server'
        vclass = conf_get_vobject(ctx, &var);
        if (!vclass) 
		{
            dfs_log_error(log, DFS_LOG_ERROR, 0,
                "at line %d: Unknown directive \"%V\"", line, &var);
			
            return NGX_ERROR;
        }

		//
        if (conf_parse_object_att(ctx, vclass, &att, word, args->nelts,
            vclass->option) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

static conf_variable_t * conf_get_vobject(conf_context_t *ctx, 
	                                            string_t *name)
{
    uint32_t         i = 0;
    conf_variable_t *v = nullptr;

    v = (conf_variable_t *)ctx->conf_objects_array->elts;

    for (i = 0; i < ctx->conf_objects_array->nelts; i++) 
	{
        if (name->len != v[i].name.len) 
		{
            continue;
        }
		
        if (string_xxstrncasecmp(name->data, v[i].name.data, name->len)) 
		{
            continue;
        }

        return (v + i);
    }

    dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
        "conf_get_vobject: not such object exists \"%V\", at line:%d",
        name, ctx->conf_line);

    return nullptr;
}

static int conf_parse_object(conf_context_t *ctx, string_t *var, 
	                               conf_object_t *obj)
{
    conf_variable_t *v = nullptr;

    v = (conf_variable_t *)array_push(ctx->conf_objects_array);
    if (v == nullptr) 
	{
        dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
            "No more space to parse configure", ctx->conf_objects_array->nelts);
		
        return NGX_ERROR;
    }
	
    //var->name = *variable;
    if (conf_string_pdup(ctx->pool,
        &v->name, var) == NGX_ERROR)
    {
        return NGX_ERROR;
    }
	
    if (conf_string_pdup(ctx->pool,
        &v->obj_name, &obj->name) == NGX_ERROR)
    {
        return NGX_ERROR;
    }
	
    // malloc memory from conf
    // conf init
    v->conf = obj->init(ctx->pool); // conf_server_init to get conf_server_t
    if (v->conf == nullptr) 
	{
        return NGX_ERROR;
    }

    v->option = obj->option;
    v->make_default = obj->make_default;

    return NGX_OK;
}

static int conf_parse_object_att(conf_context_t *ctx, conf_variable_t *v, 
	                                    string_t *att, string_t *args, 
	                                    int args_n, conf_option_t *option)
{
    uint32_t  i = 0;
    int       type;
    string_t *oper = &args[1];

    for (i = 0; option[i].option.len > 0; i++) 
	{
        if (att->len != option[i].option.len) 
		{
            continue;
        }

        if (string_xxstrncasecmp(att->data, option[i].option.data, att->len)) 
		{
            continue;
        }

        if (oper->len != 1) 
		{
            return NGX_ERROR;
        } 
		else if ((option[i].type == OPE_EQUAL) && (*oper->data == '=')) 
		{
            type = OPE_EQUAL;
        } 
		else if ((option[i].type == OPE_REGEX) && (*oper->data == '~')) 
		{
            type = OPE_REGEX;
        } 
		else 
		{
            return NGX_ERROR;
        }

        if (option[i].handler(v, option[i].offset, type, args, args_n) < 0) 
		{
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    dfs_log_error(ctx->log, DFS_LOG_ERROR, 0,
        "class \"%V\" not have this option \"%V\", at line:%d",
        &v->name, att, ctx->conf_line);
	
    return NGX_ERROR;
}

static int conf_string_pdup(pool_t *pool, string_t *dst, string_t *src)
{
    dst->data = string_xxpdup(pool, src);
    if (!dst->data) 
	{
        return NGX_ERROR;
    }
	
    dst->len = src->len;
	
    return NGX_OK;
}

// 
void * conf_get_parsed_obj(conf_context_t *ctx, string_t *name)
{
    uint32_t         i = 0;
    conf_variable_t *v = nullptr;
    array_t         *vars = nullptr;

    vars = ctx->conf_objects_array;
    v = (conf_variable_t *)vars->elts;
    for (i = 0; i < vars->nelts; i++) 
	{
        if (v[i].obj_name.len != name->len || 
            string_strncmp(v[i].obj_name.data, name->data ,name->len)) 
        {
            continue;
        }
		
        return v[i].conf;
    }
	
    return nullptr;
}

