
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * 创建一个新的链表并返回其指针。
 * @param pool 内存池指针，用于分配链表结构体
 * @param n 链表的初始容量
 * @param size 每个元素的大小
 * @return 返回创建的链表指针，创建失败时返回 NULL
 */
ngx_list_t *
ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    ngx_list_t  *list;

    // 从内存池中分配 ngx_list_t 结构体的内存块
    list = ngx_palloc(pool, sizeof(ngx_list_t));
    if (list == NULL) {
        return NULL;
    }

    // 初始化链表，设置链表的首个部分，大小，初始容量，内存池等信息
    if (ngx_list_init(list, pool, n, size) != NGX_OK) {
        return NULL;
    }

    // 返回创建的链表指针
    return list;
}



/**
 * 向链表中推入一个新元素，并返回该元素的指针。
 * @param l 链表指针
 * @return 返回推入的新元素的指针，分配内存失败时返回 NULL
 */
void *
ngx_list_push(ngx_list_t *l)
{
    void             *elt;
    ngx_list_part_t  *last;

    // 获取链表的最后一个部分
    last = l->last;

    // 如果最后一个部分已满，需要分配一个新的链表部分
    if (last->nelts == l->nalloc) {

        // 分配一个新的链表部分结构体
        last = ngx_palloc(l->pool, sizeof(ngx_list_part_t));
        if (last == NULL) {
            return NULL;
        }

        // 分配新部分的元素数组
        last->elts = ngx_palloc(l->pool, l->nalloc * l->size);
        if (last->elts == NULL) {
            return NULL;
        }

        // 初始化新部分的属性
        last->nelts = 0;
        last->next = NULL;

        // 将新部分链接到链表中
        l->last->next = last;
        l->last = last;
    }

    // 计算新元素的指针位置，然后递增最后一个部分的已使用元素数量
    elt = (char *) last->elts + l->size * last->nelts;
    last->nelts++;

    // 返回新元素的指针
    return elt;
}

