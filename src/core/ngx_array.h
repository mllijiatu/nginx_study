
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_ARRAY_H_INCLUDED_
#define _NGX_ARRAY_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

// 定义动态数组结构体
typedef struct {
    void        *elts;    // 指向实际存储数据的数组指针
    ngx_uint_t   nelts;   // 当前数组中已使用的元素个数
    size_t       size;    // 每个元素的大小
    ngx_uint_t   nalloc;  // 数组中已分配的元素个数
    ngx_pool_t  *pool;    // 内存池指针
} ngx_array_t;

// 创建一个动态数组
ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);

// 销毁动态数组
void ngx_array_destroy(ngx_array_t *a);

// 向数组中添加一个元素，并返回该元素的指针
void *ngx_array_push(ngx_array_t *a);

// 向数组中添加n个元素，并返回添加的第一个元素的指针
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);

#endif /* _NGX_ARRAY_H_INCLUDED_ */



static ngx_inline ngx_int_t
ngx_array_init(ngx_array_t *array, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    /*
     * set "array->nelts" before "array->elts", otherwise MSVC thinks
     * that "array->nelts" may be used without having been initialized
     */
    
    // 在初始化数组元素之前设置 "array->nelts"，以避免 MSVC 认为 "array->nelts" 可能在未初始化的情况下被使用

    // 将数组元素个数、元素大小、已分配的元素个数和内存池指针设置为传入的值
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;

    // 为数组分配内存，大小为 n * size，即分配 n 个大小为 size 的元素的空间
    array->elts = ngx_palloc(pool, n * size);

    // 如果内存分配失败，返回 NGX_ERROR，表示初始化失败
    if (array->elts == NULL) {
        return NGX_ERROR;
    }

    // 返回 NGX_OK，表示初始化成功
    return NGX_OK;
}



#endif /* _NGX_ARRAY_H_INCLUDED_ */
