
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * 创建一个临时缓冲区，并返回指向该缓冲区的指针。
 * 参数:
 *   - ngx_pool_t *pool: 内存池，用于分配缓冲区及其内容的内存空间。
 *   - size_t size: 缓冲区的大小。
 * 返回值:
 *   - ngx_buf_t *: 指向新创建的临时缓冲区的指针。
 */
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;  // 定义缓冲区指针

    // 调用 ngx_calloc_buf() 分配一个缓冲区结构体
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    // 通过 ngx_palloc() 为缓冲区内容分配内存空间
    b->start = ngx_palloc(pool, size);
    if (b->start == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    /*
     * ngx_calloc_buf() 设置的初始值:
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     以及一些标志位
     */

    // 设置缓冲区的位置、结束位置、以及临时标志位
    b->pos = b->start;
    b->last = b->start;
    b->end = b->last + size;
    b->temporary = 1;

    return b;  // 返回指向新创建的临时缓冲区的指针
}



/*
 * 为链表中的一个链节点分配内存，并返回指向该链节点的指针。
 * 参数:
 *   - ngx_pool_t *pool: 内存池，用于分配链节点的内存空间。
 * 返回值:
 *   - ngx_chain_t *: 指向新创建的链节点的指针。
 */
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;  // 定义链节点指针

    cl = pool->chain;  // 从内存池的链表中获取一个链节点

    if (cl) {
        pool->chain = cl->next;  // 如果链表非空，更新链表头指针
        return cl;  // 返回获取的链节点指针
    }

    cl = ngx_palloc(pool, sizeof(ngx_chain_t));  // 从内存池分配一个新的链节点
    if (cl == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    return cl;  // 返回指向新创建的链节点的指针
}



/*
 * 创建包含多个缓冲区链节点的链表，并返回指向链表头的指针。
 * 参数:
 *   - ngx_pool_t *pool: 内存池，用于分配缓冲区及链节点的内存空间。
 *   - ngx_bufs_t *bufs: 缓冲区的参数，包括缓冲区个数和大小。
 * 返回值:
 *   - ngx_chain_t *: 指向新创建的缓冲区链表的头指针。
 */
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;         // 用于分配缓冲区内存的指针
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;

    p = ngx_palloc(pool, bufs->num * bufs->size);  // 从内存池分配存储所有缓冲区的内存空间
    if (p == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {

        b = ngx_calloc_buf(pool);  // 从内存池分配缓冲区结构体
        if (b == NULL) {
            return NULL;  // 如果分配失败，返回空指针
        }

        /*
         * ngx_calloc_buf() 设置的初始值:
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     以及一些标志位
         */

        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        p += bufs->size;
        b->end = p;

        cl = ngx_alloc_chain_link(pool);  // 从内存池分配链节点
        if (cl == NULL) {
            return NULL;  // 如果分配失败，返回空指针
        }

        cl->buf = b;
        *ll = cl;
        ll = &cl->next;
    }

    *ll = NULL;  // 将链表的最后一个节点的 next 指针置为 NULL

    return chain;  // 返回指向新创建的缓冲区链表的头指针
}



/*
 * 将一个链表（in）的缓冲区链节点复制到另一个链表（chain）的末尾。
 * 参数:
 *   - ngx_pool_t *pool: 内存池，用于分配链节点的内存空间。
 *   - ngx_chain_t **chain: 指向目标链表头指针的指针。
 *   - ngx_chain_t *in: 源链表的头指针。
 * 返回值:
 *   - ngx_int_t: 成功返回 NGX_OK，失败返回 NGX_ERROR。
 */
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;

    // 遍历目标链表找到尾部
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    // 遍历源链表，复制每个缓冲区链节点到目标链表的末尾
    while (in) {
        cl = ngx_alloc_chain_link(pool);  // 分配新的链节点
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;  // 如果分配失败，返回 NGX_ERROR
        }

        cl->buf = in->buf;  // 复制缓冲区
        *ll = cl;
        ll = &cl->next;
        in = in->next;
    }

    *ll = NULL;  // 将目标链表的最后一个节点的 next 指针置为 NULL

    return NGX_OK;  // 返回成功状态码 NGX_OK
}



/*
 * 从空闲链表中获取一个空闲的缓冲区链节点，或者分配一个新的链节点和缓冲区。
 * 参数:
 *   - ngx_pool_t *p: 内存池，用于分配链节点和缓冲区的内存空间。
 *   - ngx_chain_t **free: 指向空闲链表头指针的指针。
 * 返回值:
 *   - ngx_chain_t *: 指向获取的或新分配的链节点的指针。
 */
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;

    // 如果空闲链表非空，从中取出一个链节点并返回
    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }

    // 如果空闲链表为空，分配一个新的链节点
    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    // 为新链节点分配一个新的缓冲区
    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
        return NULL;  // 如果分配失败，返回空指针
    }

    cl->next = NULL;  // 初始化链节点的 next 指针为 NULL

    return cl;  // 返回指向获取的或新分配的链节点的指针
}



/*
 * 更新空闲链表、忙碌链表和输出链表，根据缓冲区的标签。
 * 参数:
 *   - ngx_pool_t *p: 内存池，用于释放链节点和缓冲区的内存空间。
 *   - ngx_chain_t **free: 指向空闲链表头指针的指针。
 *   - ngx_chain_t **busy: 指向忙碌链表头指针的指针。
 *   - ngx_chain_t **out: 指向输出链表头指针的指针。
 *   - ngx_buf_tag_t tag: 缓冲区标签，用于判断是否属于当前操作的缓冲区。
 */
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    // 将输出链表连接到忙碌链表的末尾
    if (*out) {
        if (*busy == NULL) {
            *busy = *out;
        } else {
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }
            cl->next = *out;
        }
        *out = NULL;
    }

    // 遍历忙碌链表
    while (*busy) {
        cl = *busy;

        // 如果缓冲区的标签不符合要求，将该链节点释放并从忙碌链表中移除
        if (cl->buf->tag != tag) {
            *busy = cl->next;
            ngx_free_chain(p, cl);
            continue;
        }

        // 如果缓冲区非空，停止遍历
        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }

        // 重置缓冲区的位置指针，并将该链节点从忙碌链表移到空闲链表的头部
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        *busy = cl->next;
        cl->next = *free;
        *free = cl;
    }
}



/*
 * 合并文件缓冲区链表中的多个缓冲区，直到达到指定的限制大小或链表结束。
 * 参数:
 *   - ngx_chain_t **in: 指向文件缓冲区链表头指针的指针，该指针将被更新以指向下一个未合并的链节点。
 *   - off_t limit: 合并的限制大小。
 * 返回值:
 *   - off_t: 合并的总大小。
 */
off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    do {
        size = cl->buf->file_last - cl->buf->file_pos;

        if (size > limit - total) {
            size = limit - total;

            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);

            if (aligned <= cl->buf->file_last) {
                size = aligned - cl->buf->file_pos;
            }

            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;  // 更新输入链表头指针以指向下一个未合并的链节点

    return total;  // 返回合并的总大小
}



/*
 * 更新已发送的文件缓冲区链表，根据已发送的字节数。
 * 参数:
 *   - ngx_chain_t *in: 输入的文件缓冲区链表头指针。
 *   - off_t sent: 已发送的字节数。
 * 返回值:
 *   - ngx_chain_t *: 更新后的文件缓冲区链表头指针，指向下一个未更新的链节点。
 */
ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    // 遍历输入的文件缓冲区链表
    for ( /* void */ ; in; in = in->next) {

        // 跳过特殊缓冲区
        if (ngx_buf_special(in->buf)) {
            continue;
        }

        // 如果已发送的字节数为 0，结束遍历
        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf);

        // 如果已发送的字节数大于等于当前缓冲区的大小，继续减去缓冲区大小
        if (sent >= size) {
            sent -= size;

            // 更新缓冲区的位置指针
            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        // 已发送的字节数小于当前缓冲区的大小，更新缓冲区的位置指针
        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;  // 返回更新后的文件缓冲区链表头指针，指向下一个未更新的链节点
}

