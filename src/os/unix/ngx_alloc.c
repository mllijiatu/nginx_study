
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_uint_t  ngx_pagesize;
ngx_uint_t  ngx_pagesize_shift;
ngx_uint_t  ngx_cacheline_size;


/**
 * 分配指定大小的内存块，并返回指向该内存块的指针。
 * @param size 要分配的内存块大小
 * @param log 日志对象指针
 * @return 返回分配的内存块的指针，分配失败时返回 NULL
 */
void *
ngx_alloc(size_t size, ngx_log_t *log)
{
    void  *p;

    // 调用标准库函数 malloc 分配内存
    p = malloc(size);
    if (p == NULL) {
        // 如果分配失败，记录错误日志
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "malloc(%uz) failed", size);
    }

    // 记录调试日志
    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, log, 0, "malloc: %p:%uz", p, size);

    // 返回分配的内存块指针
    return p;
}

/**
 * 分配指定大小的内存块，并将其内容初始化为零。
 * @param size 要分配的内存块大小
 * @param log 日志对象指针
 * @return 返回分配的内存块的指针，分配失败时返回 NULL
 */
void *
ngx_calloc(size_t size, ngx_log_t *log)
{
    void  *p;

    // 调用 ngx_alloc 函数分配内存
    p = ngx_alloc(size, log);

    // 如果分配成功，将内存块内容初始化为零
    if (p) {
        ngx_memzero(p, size);
    }

    // 返回分配的内存块指针
    return p;
}



#if (NGX_HAVE_POSIX_MEMALIGN)

void *
ngx_memalign(size_t alignment, size_t size, ngx_log_t *log)
{
    void  *p;
    int    err;

    err = posix_memalign(&p, alignment, size);

    if (err) {
        ngx_log_error(NGX_LOG_EMERG, log, err,
                      "posix_memalign(%uz, %uz) failed", alignment, size);
        p = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_ALLOC, log, 0,
                   "posix_memalign: %p:%uz @%uz", p, size, alignment);

    return p;
}

#elif (NGX_HAVE_MEMALIGN)

void *
ngx_memalign(size_t alignment, size_t size, ngx_log_t *log)
{
    void  *p;

    p = memalign(alignment, size);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "memalign(%uz, %uz) failed", alignment, size);
    }

    ngx_log_debug3(NGX_LOG_DEBUG_ALLOC, log, 0,
                   "memalign: %p:%uz @%uz", p, size, alignment);

    return p;
}

#endif
