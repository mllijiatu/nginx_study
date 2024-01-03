
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_inline void *ngx_palloc_small(ngx_pool_t *pool, size_t size,
    ngx_uint_t align);
static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;  // 指向创建的内存池的指针

    // 使用 ngx_memalign 函数分配内存池，并检查分配是否成功
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }

    // 初始化内存池的基本信息
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);  // 指向未分配内存的起始位置
    p->d.end = (u_char *) p + size;                 // 指向内存池结束位置
    p->d.next = NULL;                               // 下一个内存池指针设为NULL
    p->d.failed = 0;                                 // 失败次数初始化为0

    // 计算内存池的最大容量，确保一次分配的内存块不能超过这个大小
    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    // 当前内存池指针指向创建的内存池
    p->current = p;
    p->chain = NULL;     // 内存块链表初始化为空
    p->large = NULL;     // 大块内存链表初始化为空
    p->cleanup = NULL;   // 清理函数链表初始化为空
    p->log = log;        // 记录日志的指针

    return p;  // 返回创建的内存池指针
}



void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;           // 用于遍历内存池链表的指针
    ngx_pool_large_t    *l;                // 用于遍历大块内存链表的指针
    ngx_pool_cleanup_t  *c;                // 用于遍历清理函数链表的指针

    // 遍历清理函数链表，执行清理操作
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }
// #if 和 #endif 是 C 语言中的预处理器指令，用于条件编译。这两个指令一起用于标识一段条件编译的代码块，只有当给定的条件为真（非零）时，才会包含在编译过程中，否则将被忽略。
#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    // 输出调试信息，记录要释放的大块内存的地址
    for (l = pool->large; l; l = l->next) {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
    }

    // 输出调试信息，记录要释放的内存池的地址和未使用的内存大小
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    // 释放大块内存链表中的每个大块内存
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    // 释放内存池链表中的每个内存池
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}



void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;          // 用于遍历内存池链表的指针
    ngx_pool_large_t  *l;          // 用于遍历大块内存链表的指针

    // 释放大块内存链表中的每个大块内存
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    // 遍历内存池链表，重置每个内存池的状态
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);  // 将未分配内存的起始位置重置为内存池起始位置之后
        p->d.failed = 0;                                 // 失败次数重置为0
    }

    pool->current = pool;   // 当前内存池指针指向初始的内存池
    pool->chain = NULL;     // 内存块链表重置为空
    pool->large = NULL;     // 大块内存链表重置为空
}



/**
 * 从内存池中分配大小为 size 的内存块。
 * @param pool 内存池指针
 * @param size 要分配的内存块大小
 * @return 返回分配的内存块的指针
 */
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    #if !(NGX_DEBUG_PALLOC)
        // 如果 size 小于等于内存池的最大可分配大小，则使用 ngx_palloc_small 进行分配
        if (size <= pool->max) {
            return ngx_palloc_small(pool, size, 1);
        }
    #endif

    // 否则，使用 ngx_palloc_large 进行分配
    return ngx_palloc_large(pool, size);
}

/**
 * 从内存池中分配大小为 size 的内存块，但不进行内存清零。
 * @param pool 内存池指针
 * @param size 要分配的内存块大小
 * @return 返回分配的内存块的指针
 */
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    #if !(NGX_DEBUG_PALLOC)
        // 如果 size 小于等于内存池的最大可分配大小，则使用 ngx_palloc_small 进行分配
        if (size <= pool->max) {
            return ngx_palloc_small(pool, size, 0);
        }
    #endif

    // 否则，使用 ngx_palloc_large 进行分配
    return ngx_palloc_large(pool, size);
}



static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;     // 指向分配的内存块的指针
    ngx_pool_t  *p;     // 指向当前内存池的指针

    // 获取当前内存池指针
    p = pool->current;

    // 在当前内存池及其后续内存池中查找可用的内存块
    do {
        m = p->d.last;  // 获取当前内存池未分配内存的起始位置

        // 如果需要对齐，调整 m 到合适的地址
        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }

        // 检查当前内存池是否有足够的剩余空间分配请求的内存大小
        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;  // 更新当前内存池的未分配内存位置

            return m;  // 返回分配的内存块的指针
        }

        // 切换到下一个内存池
        p = p->d.next;

    } while (p);

    // 如果所有内存池都无法满足请求，调用 ngx_palloc_block 函数分配新的内存块
    return ngx_palloc_block(pool, size);
}



static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;     // 指向分配的内存块的指针
    size_t       psize; // 内存块的总大小（包括 ngx_pool_t 结构体）
    ngx_pool_t  *p, *new; // 内存池指针和新分配的内存池指针

    // 计算内存块的总大小
    psize = (size_t) (pool->d.end - (u_char *) pool);

    // 使用 ngx_memalign 分配内存块
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;
    }

    // 将新分配的内存块视为 ngx_pool_t 结构体
    new = (ngx_pool_t *) m;

    // 初始化新的内存块信息
    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    // 调整 m 到 ngx_pool_data_t 结构体之后的位置，并进行对齐
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;  // 设置新内存块的未分配内存位置

    // 遍历当前内存池链表，更新 failed 字段
    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }

    // 将新的内存块添加到当前内存池链表的末尾
    p->d.next = new;

    // 返回新内存块的未分配内存位置
    return m;
}



static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;        // 指向分配的大块内存的指针
    ngx_uint_t         n;        // 记录循环次数
    ngx_pool_large_t  *large;    // 大块内存链表节点指针

    // 使用 ngx_alloc 函数分配大块内存
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    // 遍历大块内存链表，查找未使用的节点
    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }

        // 如果遍历次数超过 3 次，跳出循环
        if (n++ > 3) {
            break;
        }
    }

    // 如果大块内存链表中没有可用节点，分配一个新的 ngx_pool_large_t 结构体
    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    // 初始化新节点信息
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}



void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


ngx_int_t 
ngx_pfree(ngx_pool_t *pool, void *p)
{
    // 定义一个指向ngx_pool_large_t结构体的指针l，用于遍历pool中的large链表
    ngx_pool_large_t  *l;

    // 遍历pool中的large链表
    for (l = pool->large; l; l = l->next) {
        // 检查要释放的内存指针p是否等于当前large节点的alloc指针
        if (p == l->alloc) {
            // 在调试日志中记录要释放的内存地址
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            // 使用ngx_free释放large节点中的内存
            ngx_free(l->alloc);
            // 将large节点的alloc指针置为NULL，表示该内存已被释放
            l->alloc = NULL;

            // 返回NGX_OK表示成功释放内存
            return NGX_OK;
        }
    }

    // 如果未找到要释放的内存指针，返回NGX_DECLINED表示释放失败
    return NGX_DECLINED;
}



/*
 * 使用内存池分配内存，并将分配的内存清零
 * 该函数先使用 ngx_palloc 函数分配内存，然后调用 ngx_memzero 将内存清零
 * 参数：
 *   - pool: 内存池指针
 *   - size: 分配的内存大小
 * 返回分配的内存指针，如果分配失败则返回 NULL
 */
void *ngx_pcalloc(ngx_pool_t *pool, size_t size) {
    void *p;

    p = ngx_palloc(pool, size);  // 使用内存池分配内存
    if (p) {
        ngx_memzero(p, size);  // 将分配的内存清零
    }

    return p;  // 返回分配的内存指针，如果分配失败则返回 NULL
}



ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    // 定义一个指向ngx_pool_cleanup_t结构体的指针c
    ngx_pool_cleanup_t  *c;

    // 分配一个ngx_pool_cleanup_t结构体的内存
    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;  // 分配失败，返回空指针
    }

    // 如果size不为0，分配size大小的内存，并将其赋给cleanup的data字段
    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;  // 分配失败，返回空指针
        }
    } else {
        c->data = NULL;  // 如果size为0，将data字段置为NULL
    }

    // 将handler和next字段置为NULL，表示清理处理函数和下一个cleanup节点为空
    c->handler = NULL;
    c->next = p->cleanup;

    // 将新的cleanup节点加入到内存池的cleanup链表头部
    p->cleanup = c;

    // 在调试日志中记录添加cleanup节点的信息
    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    // 返回新添加的cleanup节点
    return c;
}


void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;     // 定义内存池清理操作的结构体指针
    ngx_pool_cleanup_file_t  *cf;    // 定义文件清理操作的结构体指针

    // 遍历内存池的清理操作链表
    for (c = p->cleanup; c; c = c->next) {
        // 检查清理操作的处理函数是否为文件清理函数
        if (c->handler == ngx_pool_cleanup_file) {

            // 将清理操作的数据指针转换为文件清理操作的结构体指针
            cf = c->data;

            // 如果文件描述符匹配，执行清理操作的处理函数，并将处理函数置为空
            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}



void ngx_pool_cleanup_file(void *data)
{
    // 将传入的数据指针转换为文件清理操作的结构体指针
    ngx_pool_cleanup_file_t  *c = data;

    // 在调试日志中记录文件清理的相关信息，包括文件描述符和日志对象
    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    // 尝试关闭文件描述符，如果失败则记录错误日志
    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}



void ngx_pool_delete_file(void *data)
{
    // 将传入的数据指针转换为文件清理操作的结构体指针
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;  // 定义错误码

    // 在调试日志中记录文件清理的相关信息，包括文件描述符、文件名和日志对象
    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    // 尝试删除文件，如果删除失败则记录错误日志，但允许文件不存在的情况
    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        // 如果错误不是文件不存在，则记录严重错误日志
        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    // 尝试关闭文件描述符，如果关闭失败则记录错误日志
    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}



#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
