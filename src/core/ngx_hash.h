
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * 结构体 ngx_hash_elt_t 表示散列表中的一个元素。
 * value 存储元素的值，len 表示元素名字的长度，name 为变长数组，存储元素名字的首地址。
 */
typedef struct {
    void             *value;
    u_short           len;
    u_char            name[1];
} ngx_hash_elt_t;

/**
 * 结构体 ngx_hash_t 表示一个散列表。
 * buckets 是一个指向 ngx_hash_elt_t 指针数组的指针，size 表示散列表的大小。
 */
typedef struct {
    ngx_hash_elt_t  **buckets;
    ngx_uint_t        size;
} ngx_hash_t;

/**
 * 结构体 ngx_hash_wildcard_t 表示带通配符的散列表。
 * hash 是一个 ngx_hash_t 结构体，value 存储与通配符匹配的值。
 */
typedef struct {
    ngx_hash_t        hash;
    void             *value;
} ngx_hash_wildcard_t;

/**
 * 结构体 ngx_hash_key_t 表示散列表的键值对。
 * key 存储键的 ngx_str_t 结构体，key_hash 存储键的哈希值，value 存储与键相关联的值。
 */
typedef struct {
    ngx_str_t         key;
    ngx_uint_t        key_hash;
    void             *value;
} ngx_hash_key_t;



typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


/**
 * 结构体 ngx_hash_combined_t 表示综合使用散列表和带通配符散列表的结构。
 * hash 是基本散列表，wc_head 和 wc_tail 是带通配符散列表的头和尾。
 */
typedef struct {
    ngx_hash_t            hash;
    ngx_hash_wildcard_t  *wc_head;
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;

/**
 * 结构体 ngx_hash_init_t 表示初始化散列表所需的参数。
 * hash 是散列表结构体指针，key 是用于计算哈希值的回调函数指针。
 * max_size 表示散列表的最大容量，bucket_size 表示每个桶的大小。
 * name 是散列表的名称，pool 是分配散列表内存的内存池，temp_pool 是临时内存池。
 */
typedef struct {
    ngx_hash_t       *hash;
    ngx_hash_key_pt   key;

    ngx_uint_t        max_size;
    ngx_uint_t        bucket_size;

    char             *name;
    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;
} ngx_hash_init_t;



#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


typedef struct {
    ngx_uint_t        hsize;

    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;

    ngx_array_t       keys;
    ngx_array_t      *keys_hash;

    ngx_array_t       dns_wc_head;
    ngx_array_t      *dns_wc_head_hash;

    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;


typedef struct ngx_table_elt_s  ngx_table_elt_t;

struct ngx_table_elt_s {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
    ngx_table_elt_t  *next;
};


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);


ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
