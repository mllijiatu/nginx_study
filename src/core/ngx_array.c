
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
/*
使用了一个 ngx_pool_t 指针和两个整数参数（n 和 size）来创建一个 ngx_array_t 结构体。
ngx_array_t 结构体通常用于管理动态数组。
函数首先通过 ngx_palloc 分配了足够大小的内存空间来存储 ngx_array_t 结构体，然后通过 ngx_array_init 对其进行初始化。
如果内存分配或初始化失败，函数会返回 NULL。否则，它将返回指向新创建的 ngx_array_t 结构体的指针。
*/
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    // 为 ngx_array_t 结构体分配内存空间
    a = ngx_palloc(p, sizeof(ngx_array_t));
    
    // 检查内存分配是否成功
    if (a == NULL) {
        return NULL;
    }

    // 初始化 ngx_array_t 结构体
    if (ngx_array_init(a, p, n, size) != NGX_OK) {
        return NULL;
    }

    // 返回创建好的 ngx_array_t 结构体指针
    return a;
}


/*
销毁 ngx_array_t 结构体，并释放其占用的内存。
首先，通过结构体中的 pool 指针获取到关联的内存池。
然后，通过比较数组的内存是否位于内存池的最后一块，来判断是否需要回退内存池的 last 指针。
接着，再次通过比较 ngx_array_t 结构体是否位于内存池的最后一块，同样判断是否需要回退内存池的 last 指针。这样，就能够释放 ngx_array_t 结构体和其管理的数组所占用的内存。
*/
void
ngx_array_destroy(ngx_array_t *a)
{
    ngx_pool_t  *p;

    // 获取 ngx_array_t 结构体中的 ngx_pool_t 指针
    p = a->pool;

    // 检查数组的内存是否是在内存池的最后一块
    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        // 如果是，将内存池的 last 指针回退，释放数组占用的内存
        p->d.last -= a->size * a->nalloc;
    }

    // 检查 ngx_array_t 结构体是否是在内存池的最后一块
    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        // 如果是，将内存池的 last 指针回退，释放 ngx_array_t 占用的内存
        p->d.last = (u_char *) a;
    }
}


/*
动态数组中添加元素。
如果数组已满，则需要进行扩容。
如果当前数组的内存是内存池的最后一块，并且有足够的空间进行新的分配，就直接扩容。
否则，就分配一个新的数组，将原数组的内容拷贝到新数组中，并更新数组的指针和容量。
最后，计算新元素的地址并增加数组元素计数，然后返回新元素的地址。
*/
void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    // 检查数组是否已满，如果是，则需要进行扩容
    if (a->nelts == a->nalloc) {

        /* 数组已满 */

        // 计算当前数组占用的总内存大小
        size = a->size * a->nalloc;

        // 获取数组关联的内存池指针
        p = a->pool;

        // 检查数组的内存是否是内存池的最后一块，并且还有足够的空间供扩容
        if ((u_char *) a->elts + size == p->d.last
            && p->d.last + a->size <= p->d.end)
        {
            /*
             * 数组的内存是内存池的最后一块
             * 并且有足够的空间进行新的分配
             */

            // 将内存池的 last 指针移动到下一个可用的位置
            p->d.last += a->size;
            // 增加数组的容量
            a->nalloc++;

        } else {
            /* 分配一个新的数组 */

            // 分配两倍于当前大小的内存空间
            new = ngx_palloc(p, 2 * size);
            if (new == NULL) {
                return NULL;
            }

            // 将原数组的内容拷贝到新数组中
            ngx_memcpy(new, a->elts, size);
            // 更新数组的指针和容量
            a->elts = new;
            a->nalloc *= 2;
        }
    }

    // 计算新元素的地址并增加数组元素计数
    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;

    // 返回新元素的地址
    return elt;
}


/*
函数首先计算要添加的元素的总内存大小，然后检查数组是否已满。如果数组已满，则进行扩容。
如果当前数组的内存是内存池的最后一块，并且有足够的空间进行新的分配，就直接扩容。
否则，就分配一个新的数组，将原数组的内容拷贝到新数组中，并更新数组的指针和容量。
最后，计算新元素的地址并增加数组元素计数，然后返回新元素的地址。
*/
void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    // 计算要添加的元素的总内存大小
    size = n * a->size;

    // 如果数组已满，则需要进行扩容
    if (a->nelts + n > a->nalloc) {

        /* 数组已满 */

        // 获取数组关联的内存池指针
        p = a->pool;

        // 检查数组的内存是否是内存池的最后一块，并且还有足够的空间供扩容
        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last
            && p->d.last + size <= p->d.end)
        {
            /*
             * 数组的内存是内存池的最后一块
             * 并且有足够的空间进行新的分配
             */

            // 将内存池的 last 指针移动到下一个可用的位置
            p->d.last += size;
            // 增加数组的容量
            a->nalloc += n;

        } else {
            /* 分配一个新的数组 */

            // 计算新的数组容量，取 n 和原容量的两者中较大的值的两倍
            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);

            // 分配新的内存空间            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            // 将原数组的内容拷贝到新数组中
            ngx_memcpy(new, a->elts, a->nelts * a->size);
            // 更新数组的指针和容量
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    // 计算新元素的地址并增加数组元素计数
    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    // 返回新元素的地址
    return elt;
}
