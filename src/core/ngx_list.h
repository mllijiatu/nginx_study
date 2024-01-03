
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LIST_H_INCLUDED_
#define _NGX_LIST_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_list_part_s  ngx_list_part_t;

/**
 * 结构体 ngx_list_part_s 表示链表的一个部分，每个部分包含一组元素。
 * 指针 elts 指向元素数组，nelts 表示当前部分中已使用的元素数量，next 指向下一个部分。
 */
struct ngx_list_part_s {
    void             *elts;
    ngx_uint_t        nelts;
    ngx_list_part_t  *next;
};

/**
 * 结构体 ngx_list_t 表示一个链表，通过多个 ngx_list_part_s 组成。
 * last 指向最后一个部分，part 是链表的第一个部分，size 表示每个元素的大小，nalloc 表示每个部分可以容纳的元素数量。
 * pool 指向链表所属的内存池。
 */
typedef struct {
    ngx_list_part_t  *last;
    ngx_list_part_t   part;
    size_t            size;
    ngx_uint_t        nalloc;
    ngx_pool_t       *pool;
} ngx_list_t;



ngx_list_t *ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size);

static ngx_inline ngx_int_t
ngx_list_init(ngx_list_t *list, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    list->part.elts = ngx_palloc(pool, n * size);
    if (list->part.elts == NULL) {
        return NGX_ERROR;
    }

    list->part.nelts = 0;
    list->part.next = NULL;
    list->last = &list->part;
    list->size = size;
    list->nalloc = n;
    list->pool = pool;

    return NGX_OK;
}


/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */


void *ngx_list_push(ngx_list_t *list);


#endif /* _NGX_LIST_H_INCLUDED_ */
