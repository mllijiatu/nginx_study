
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_STRING_H_INCLUDED_
#define _NGX_STRING_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * 结构体 ngx_str_t 表示一个字节字符串，包含字符串的长度 len 和数据指针 data。
 */
typedef struct {
    size_t      len;
    u_char     *data;
} ngx_str_t;

/**
 * 结构体 ngx_keyval_t 表示一个键值对，包含键和值各自的 ngx_str_t 结构体。
 */
typedef struct {
    ngx_str_t   key;
    ngx_str_t   value;
} ngx_keyval_t;



/**
 * 结构体 ngx_variable_value_t 表示一个变量值，包含长度 len、标志位 valid、no_cacheable、not_found、escape 和数据指针 data。
 */
typedef struct {
    unsigned    len:28;         /* 变量值的长度 */

    unsigned    valid:1;        /* 变量值是否有效的标志位 */
    unsigned    no_cacheable:1; /* 变量值是否不可缓存的标志位 */
    unsigned    not_found:1;    /* 变量值是否未找到的标志位 */
    unsigned    escape:1;       /* 变量值是否已转义的标志位 */

    u_char     *data;           /* 变量值的数据指针 */
} ngx_variable_value_t;



/**
 * 宏定义 ngx_string 用于快速初始化 ngx_str_t 结构体，表示一个字符串常量。
 * @param str 字符串常量
 * @return 返回 ngx_str_t 结构体
 */
#define ngx_string(str)     { sizeof(str) - 1, (u_char *) str }

/**
 * 宏定义 ngx_null_string 用于快速初始化 ngx_str_t 结构体，表示一个空字符串。
 */
#define ngx_null_string     { 0, NULL }

/**
 * 宏定义 ngx_str_set 用于设置 ngx_str_t 结构体的值，将字符串文本转换为 ngx_str_t。
 * @param str ngx_str_t 结构体指针
 * @param text 字符串文本
 */
#define ngx_str_set(str, text) \
    (str)->len = sizeof(text) - 1; (str)->data = (u_char *) text

/**
 * 宏定义 ngx_str_null 用于将 ngx_str_t 结构体设置为空字符串。
 * @param str ngx_str_t 结构体指针
 */
#define ngx_str_null(str)   (str)->len = 0; (str)->data = NULL

/**
 * 宏定义 ngx_tolower 用于将字符转换为小写字母。
 * @param c 待转换的字符
 * @return 返回转换后的字符
 */
#define ngx_tolower(c)      (u_char) ((c >= 'A' && c <= 'Z') ? (c | 0x20) : c)

/**
 * 宏定义 ngx_toupper 用于将字符转换为大写字母。
 * @param c 待转换的字符
 * @return 返回转换后的字符
 */
#define ngx_toupper(c)      (u_char) ((c >= 'a' && c <= 'z') ? (c & ~0x20) : c)


void ngx_strlow(u_char *dst, u_char *src, size_t n);


#define ngx_strncmp(s1, s2, n)  strncmp((const char *) s1, (const char *) s2, n)


/* msvc and icc7 compile strcmp() to inline loop */
#define ngx_strcmp(s1, s2)  strcmp((const char *) s1, (const char *) s2)


#define ngx_strstr(s1, s2)  strstr((const char *) s1, (const char *) s2)
#define ngx_strlen(s)       strlen((const char *) s)

size_t ngx_strnlen(u_char *p, size_t n);

#define ngx_strchr(s1, c)   strchr((const char *) s1, (int) c)

static ngx_inline u_char *
ngx_strlchr(u_char *p, u_char *last, u_char c)
{
    while (p < last) {

        if (*p == c) {
            return p;
        }

        p++;
    }

    return NULL;
}


/*
 * msvc and icc7 compile memset() to the inline "rep stos"
 * while ZeroMemory() and bzero() are the calls.
 * icc7 may also inline several mov's of a zeroed register for small blocks.
 */

/**
 * 宏定义 ngx_memzero 用于将指定内存块的内容全部置零。
 * @param buf 要清零的内存块的指针
 * @param n 内存块的大小
 */
#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)

/**
 * 宏定义 ngx_memset 用于将指定内存块的内容全部设置为指定值。
 * @param buf 要设置的内存块的指针
 * @param c 要设置的值
 * @param n 内存块的大小
 */
#define ngx_memset(buf, c, n)     (void) memset(buf, c, n)


void ngx_explicit_memzero(void *buf, size_t n);


#if (NGX_MEMCPY_LIMIT)

void *ngx_memcpy(void *dst, const void *src, size_t n);
#define ngx_cpymem(dst, src, n)   (((u_char *) ngx_memcpy(dst, src, n)) + (n))

#else

/*
 * gcc3, msvc, and icc7 compile memcpy() to the inline "rep movs".
 * gcc3 compiles memcpy(d, s, 4) to the inline "mov"es.
 * icc8 compile memcpy(d, s, 4) to the inline "mov"es or XMM moves.
 */
/*
 * 定义 ngx_memcpy 宏，使用 memcpy 复制内存块
 * 参数：
 *   - dst: 目标内存地址
 *   - src: 源内存地址
 *   - n: 复制的字节数
 */
#define ngx_memcpy(dst, src, n)   (void) memcpy(dst, src, n)

/*
 * 定义 ngx_cpymem 宏，使用 memcpy 复制内存块，并返回目标内存的尾部指针
 * 参数：
 *   - dst: 目标内存地址
 *   - src: 源内存地址
 *   - n: 复制的字节数
 */
#define ngx_cpymem(dst, src, n)   (((u_char *) memcpy(dst, src, n)) + (n))


#endif


#if ( __INTEL_COMPILER >= 800 )

/*
 * the simple inline cycle copies the variable length strings up to 16
 * bytes faster than icc8 autodetecting _intel_fast_memcpy()
 */

static ngx_inline u_char *
ngx_copy(u_char *dst, u_char *src, size_t len)
{
    if (len < 17) {

        while (len) {
            *dst++ = *src++;
            len--;
        }

        return dst;

    } else {
        return ngx_cpymem(dst, src, len);
    }
}

#else

#define ngx_copy                  ngx_cpymem

#endif


#define ngx_memmove(dst, src, n)  (void) memmove(dst, src, n)
#define ngx_movemem(dst, src, n)  (((u_char *) memmove(dst, src, n)) + (n))


/* msvc and icc7 compile memcmp() to the inline loop */
#define ngx_memcmp(s1, s2, n)     memcmp(s1, s2, n)


u_char *ngx_cpystrn(u_char *dst, u_char *src, size_t n);
u_char *ngx_pstrdup(ngx_pool_t *pool, ngx_str_t *src);
u_char * ngx_cdecl ngx_sprintf(u_char *buf, const char *fmt, ...);
u_char * ngx_cdecl ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...);
u_char * ngx_cdecl ngx_slprintf(u_char *buf, u_char *last, const char *fmt,
    ...);
u_char *ngx_vslprintf(u_char *buf, u_char *last, const char *fmt, va_list args);
#define ngx_vsnprintf(buf, max, fmt, args)                                   \
    ngx_vslprintf(buf, buf + (max), fmt, args)

ngx_int_t ngx_strcasecmp(u_char *s1, u_char *s2);
ngx_int_t ngx_strncasecmp(u_char *s1, u_char *s2, size_t n);

u_char *ngx_strnstr(u_char *s1, char *s2, size_t n);

u_char *ngx_strstrn(u_char *s1, char *s2, size_t n);
u_char *ngx_strcasestrn(u_char *s1, char *s2, size_t n);
u_char *ngx_strlcasestrn(u_char *s1, u_char *last, u_char *s2, size_t n);

ngx_int_t ngx_rstrncmp(u_char *s1, u_char *s2, size_t n);
ngx_int_t ngx_rstrncasecmp(u_char *s1, u_char *s2, size_t n);
ngx_int_t ngx_memn2cmp(u_char *s1, u_char *s2, size_t n1, size_t n2);
ngx_int_t ngx_dns_strcmp(u_char *s1, u_char *s2);
ngx_int_t ngx_filename_cmp(u_char *s1, u_char *s2, size_t n);

ngx_int_t ngx_atoi(u_char *line, size_t n);
ngx_int_t ngx_atofp(u_char *line, size_t n, size_t point);
ssize_t ngx_atosz(u_char *line, size_t n);
off_t ngx_atoof(u_char *line, size_t n);
time_t ngx_atotm(u_char *line, size_t n);
ngx_int_t ngx_hextoi(u_char *line, size_t n);

u_char *ngx_hex_dump(u_char *dst, u_char *src, size_t len);


#define ngx_base64_encoded_length(len)  (((len + 2) / 3) * 4)
#define ngx_base64_decoded_length(len)  (((len + 3) / 4) * 3)

void ngx_encode_base64(ngx_str_t *dst, ngx_str_t *src);
void ngx_encode_base64url(ngx_str_t *dst, ngx_str_t *src);
ngx_int_t ngx_decode_base64(ngx_str_t *dst, ngx_str_t *src);
ngx_int_t ngx_decode_base64url(ngx_str_t *dst, ngx_str_t *src);

uint32_t ngx_utf8_decode(u_char **p, size_t n);
size_t ngx_utf8_length(u_char *p, size_t n);
u_char *ngx_utf8_cpystrn(u_char *dst, u_char *src, size_t n, size_t len);


#define NGX_ESCAPE_URI            0
#define NGX_ESCAPE_ARGS           1
#define NGX_ESCAPE_URI_COMPONENT  2
#define NGX_ESCAPE_HTML           3
#define NGX_ESCAPE_REFRESH        4
#define NGX_ESCAPE_MEMCACHED      5
#define NGX_ESCAPE_MAIL_AUTH      6

#define NGX_UNESCAPE_URI       1
#define NGX_UNESCAPE_REDIRECT  2

uintptr_t ngx_escape_uri(u_char *dst, u_char *src, size_t size,
    ngx_uint_t type);
void ngx_unescape_uri(u_char **dst, u_char **src, size_t size, ngx_uint_t type);
uintptr_t ngx_escape_html(u_char *dst, u_char *src, size_t size);
uintptr_t ngx_escape_json(u_char *dst, u_char *src, size_t size);


typedef struct {
    ngx_rbtree_node_t         node;
    ngx_str_t                 str;
} ngx_str_node_t;


void ngx_str_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
ngx_str_node_t *ngx_str_rbtree_lookup(ngx_rbtree_t *rbtree, ngx_str_t *name,
    uint32_t hash);


void ngx_sort(void *base, size_t n, size_t size,
    ngx_int_t (*cmp)(const void *, const void *));
#define ngx_qsort             qsort


#define ngx_value_helper(n)   #n
#define ngx_value(n)          ngx_value_helper(n)


#endif /* _NGX_STRING_H_INCLUDED_ */
