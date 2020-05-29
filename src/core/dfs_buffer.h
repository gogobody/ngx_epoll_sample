#ifndef DFS_BUF_H
#define DFS_BUF_H

#include "dfs_types.h"
/*
 * pos通常是用来告诉使用者本次应该从pos这个位置开始处理内存中的数据，这样设置是因为同一个
 * ngx_buf_t可能被多次反复处理。当然，pos的含义是由使用它的模板定义的
 */
/* last通常表示有效的内容到此为止，注意，pos与last之间的内存是希望nginx处理的内容 */

struct buffer_s 
{
    uchar_t  *pos;         // write out position /* 待处理缓冲区起始位置 */ 当buf所指向的数据在内存里的时候，pos指向的是这段数据开始的位置。
    uchar_t  *last;        // read in position /* 待处理缓冲区最后一个位置 last - pos = 实际有效内容 */ 	当buf所指向的数据在内存里的时候，last指向的是这段数据结束的位置。
    off_t     file_pos;    // write out position /* 文件处理 起始位置 */
    off_t     file_last;   //  /* 文件处理 最后一个位置  */
    uchar_t  *start;       // start of buffer /* start of buffer 缓冲区起始地址 */
    //当buf所指向的数据在内存里的时候，这一整块内存包含的内容可能被包含在多个buf中(比如在某段数据中间插入了其他的数据，这一块数据就需要被拆分开)。那么这些buf中的start和end都指向这一块内存的开始地址和结束地址。而pos和last指向本buf所实际包含的数据的开始和结尾。
    uchar_t  *end;         // end of buffer  /* end of buffer 缓冲区结束地址 */
    uint32_t  temporary:1; // alloc in pool, need n ot free  /* buf指向临时内存 内存数据可以进行修改 */
    uint32_t  memory:1;    // memory, not file /* buf指向内存数据 只读 */ 为1时表示该buf所包含的内容是在内存
    uint32_t  in_file:1;   // file flag /* 表示buf指向文件 */ 为1时表示该buf所包含的内容是在文件中
};

typedef struct chunk_s chunk_t;

struct chunk_s
{
    buffer_t *hdr;  // 64-bit hexadimal string
    ssize_t   size;
    chunk_t  *next;
};

#define buffer_in_memory(b)      ((b)->memory)// || (b)->mmap || (b)->memory
#define buffer_in_memory_only(b) (buffer_in_memory((b)) && !(b)->in_file)
#define buffer_size(b) \
    (buffer_in_memory(b) ? ((b)->last - (b)->pos): \
    ((b)->file_last - (b)->file_pos))
#define buffer_free_size(b) ((b)->end - (b)->last)
#define buffer_full(b)      ((b)->end == (b)->pos)
#define buffer_reset(b)     ((b)->last = (b)->pos = (b)->start)
#define buffer_pull(b,s)    ((b)->pos += (s))
#define buffer_used(b,s)    ((b)->last += (s))

buffer_t *buffer_create(pool_t *pool, size_t size);
buffer_t *buffer_alloc(pool_t *p);
void      buffer_free(buffer_t *buf);
void      buffer_shrink(buffer_t* buf);

#endif

