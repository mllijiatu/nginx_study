
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Maxim Dounin
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_MODULE_H_INCLUDED_
#define _NGX_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


#define NGX_MODULE_UNSET_INDEX  (ngx_uint_t) -1


#define NGX_MODULE_SIGNATURE_0                                                \
    ngx_value(NGX_PTR_SIZE) ","                                               \
    ngx_value(NGX_SIG_ATOMIC_T_SIZE) ","                                      \
    ngx_value(NGX_TIME_T_SIZE) ","

#if (NGX_HAVE_KQUEUE)
#define NGX_MODULE_SIGNATURE_1   "1"
#else
#define NGX_MODULE_SIGNATURE_1   "0"
#endif

#if (NGX_HAVE_IOCP)
#define NGX_MODULE_SIGNATURE_2   "1"
#else
#define NGX_MODULE_SIGNATURE_2   "0"
#endif

#if (NGX_HAVE_FILE_AIO || NGX_COMPAT)
#define NGX_MODULE_SIGNATURE_3   "1"
#else
#define NGX_MODULE_SIGNATURE_3   "0"
#endif

#if (NGX_HAVE_SENDFILE_NODISKIO || NGX_COMPAT)
#define NGX_MODULE_SIGNATURE_4   "1"
#else
#define NGX_MODULE_SIGNATURE_4   "0"
#endif

#if (NGX_HAVE_EVENTFD)
#define NGX_MODULE_SIGNATURE_5   "1"
#else
#define NGX_MODULE_SIGNATURE_5   "0"
#endif

#if (NGX_HAVE_EPOLL)
#define NGX_MODULE_SIGNATURE_6   "1"
#else
#define NGX_MODULE_SIGNATURE_6   "0"
#endif

#if (NGX_HAVE_KEEPALIVE_TUNABLE)
#define NGX_MODULE_SIGNATURE_7   "1"
#else
#define NGX_MODULE_SIGNATURE_7   "0"
#endif

#if (NGX_HAVE_INET6)
#define NGX_MODULE_SIGNATURE_8   "1"
#else
#define NGX_MODULE_SIGNATURE_8   "0"
#endif

#define NGX_MODULE_SIGNATURE_9   "1"
#define NGX_MODULE_SIGNATURE_10  "1"

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
#define NGX_MODULE_SIGNATURE_11  "1"
#else
#define NGX_MODULE_SIGNATURE_11  "0"
#endif

#define NGX_MODULE_SIGNATURE_12  "1"

#if (NGX_HAVE_SETFIB)
#define NGX_MODULE_SIGNATURE_13  "1"
#else
#define NGX_MODULE_SIGNATURE_13  "0"
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
#define NGX_MODULE_SIGNATURE_14  "1"
#else
#define NGX_MODULE_SIGNATURE_14  "0"
#endif

#if (NGX_HAVE_UNIX_DOMAIN)
#define NGX_MODULE_SIGNATURE_15  "1"
#else
#define NGX_MODULE_SIGNATURE_15  "0"
#endif

#if (NGX_HAVE_VARIADIC_MACROS)
#define NGX_MODULE_SIGNATURE_16  "1"
#else
#define NGX_MODULE_SIGNATURE_16  "0"
#endif

#define NGX_MODULE_SIGNATURE_17  "0"
#define NGX_MODULE_SIGNATURE_18  "0"

#if (NGX_HAVE_OPENAT)
#define NGX_MODULE_SIGNATURE_19  "1"
#else
#define NGX_MODULE_SIGNATURE_19  "0"
#endif

#if (NGX_HAVE_ATOMIC_OPS)
#define NGX_MODULE_SIGNATURE_20  "1"
#else
#define NGX_MODULE_SIGNATURE_20  "0"
#endif

#if (NGX_HAVE_POSIX_SEM)
#define NGX_MODULE_SIGNATURE_21  "1"
#else
#define NGX_MODULE_SIGNATURE_21  "0"
#endif

#if (NGX_THREADS || NGX_COMPAT)
#define NGX_MODULE_SIGNATURE_22  "1"
#else
#define NGX_MODULE_SIGNATURE_22  "0"
#endif

#if (NGX_PCRE)
#define NGX_MODULE_SIGNATURE_23  "1"
#else
#define NGX_MODULE_SIGNATURE_23  "0"
#endif

#if (NGX_HTTP_SSL || NGX_COMPAT)
#define NGX_MODULE_SIGNATURE_24  "1"
#else
#define NGX_MODULE_SIGNATURE_24  "0"
#endif

#define NGX_MODULE_SIGNATURE_25  "1"

#if (NGX_HTTP_GZIP)
#define NGX_MODULE_SIGNATURE_26  "1"
#else
#define NGX_MODULE_SIGNATURE_26  "0"
#endif

#define NGX_MODULE_SIGNATURE_27  "1"

#if (NGX_HTTP_X_FORWARDED_FOR)
#define NGX_MODULE_SIGNATURE_28  "1"
#else
#define NGX_MODULE_SIGNATURE_28  "0"
#endif

#if (NGX_HTTP_REALIP)
#define NGX_MODULE_SIGNATURE_29  "1"
#else
#define NGX_MODULE_SIGNATURE_29  "0"
#endif

#if (NGX_HTTP_HEADERS)
#define NGX_MODULE_SIGNATURE_30  "1"
#else
#define NGX_MODULE_SIGNATURE_30  "0"
#endif

#if (NGX_HTTP_DAV)
#define NGX_MODULE_SIGNATURE_31  "1"
#else
#define NGX_MODULE_SIGNATURE_31  "0"
#endif

#if (NGX_HTTP_CACHE)
#define NGX_MODULE_SIGNATURE_32  "1"
#else
#define NGX_MODULE_SIGNATURE_32  "0"
#endif

#if (NGX_HTTP_UPSTREAM_ZONE)
#define NGX_MODULE_SIGNATURE_33  "1"
#else
#define NGX_MODULE_SIGNATURE_33  "0"
#endif

#if (NGX_COMPAT)
#define NGX_MODULE_SIGNATURE_34  "1"
#else
#define NGX_MODULE_SIGNATURE_34  "0"
#endif

#define NGX_MODULE_SIGNATURE                                                  \
    NGX_MODULE_SIGNATURE_0 NGX_MODULE_SIGNATURE_1 NGX_MODULE_SIGNATURE_2      \
    NGX_MODULE_SIGNATURE_3 NGX_MODULE_SIGNATURE_4 NGX_MODULE_SIGNATURE_5      \
    NGX_MODULE_SIGNATURE_6 NGX_MODULE_SIGNATURE_7 NGX_MODULE_SIGNATURE_8      \
    NGX_MODULE_SIGNATURE_9 NGX_MODULE_SIGNATURE_10 NGX_MODULE_SIGNATURE_11    \
    NGX_MODULE_SIGNATURE_12 NGX_MODULE_SIGNATURE_13 NGX_MODULE_SIGNATURE_14   \
    NGX_MODULE_SIGNATURE_15 NGX_MODULE_SIGNATURE_16 NGX_MODULE_SIGNATURE_17   \
    NGX_MODULE_SIGNATURE_18 NGX_MODULE_SIGNATURE_19 NGX_MODULE_SIGNATURE_20   \
    NGX_MODULE_SIGNATURE_21 NGX_MODULE_SIGNATURE_22 NGX_MODULE_SIGNATURE_23   \
    NGX_MODULE_SIGNATURE_24 NGX_MODULE_SIGNATURE_25 NGX_MODULE_SIGNATURE_26   \
    NGX_MODULE_SIGNATURE_27 NGX_MODULE_SIGNATURE_28 NGX_MODULE_SIGNATURE_29   \
    NGX_MODULE_SIGNATURE_30 NGX_MODULE_SIGNATURE_31 NGX_MODULE_SIGNATURE_32   \
    NGX_MODULE_SIGNATURE_33 NGX_MODULE_SIGNATURE_34


#define NGX_MODULE_V1                                                         \
    NGX_MODULE_UNSET_INDEX, NGX_MODULE_UNSET_INDEX,                           \
    NULL, 0, 0, nginx_version, NGX_MODULE_SIGNATURE

#define NGX_MODULE_V1_PADDING  0, 0, 0, 0, 0, 0, 0, 0


/*
 * 结构体 ngx_module_s 表示Nginx模块的基本信息结构
 * 该结构体用于描述Nginx中各个模块的基本属性和回调函数
 */
struct ngx_module_s {
    ngx_uint_t            ctx_index;      // 模块在配置结构中的索引
    ngx_uint_t            index;          // 模块的唯一索引

    char                 *name;           // 模块的名称

    ngx_uint_t            spare0;         // 备用字段0
    ngx_uint_t            spare1;         // 备用字段1

    ngx_uint_t            version;        // 模块的版本
    const char           *signature;      // 模块的签名

    void                 *ctx;            // 模块的上下文
    ngx_command_t        *commands;       // 模块的配置指令数组
    ngx_uint_t            type;           // 模块的类型

    ngx_int_t           (*init_master)(ngx_log_t *log);       // 主进程初始化回调函数
    ngx_int_t           (*init_module)(ngx_cycle_t *cycle);   // 模块初始化回调函数

    ngx_int_t           (*init_process)(ngx_cycle_t *cycle);  // 工作进程初始化回调函数
    ngx_int_t           (*init_thread)(ngx_cycle_t *cycle);   // 线程初始化回调函数
    void                (*exit_thread)(ngx_cycle_t *cycle);    // 线程退出回调函数
    void                (*exit_process)(ngx_cycle_t *cycle);   // 工作进程退出回调函数

    void                (*exit_master)(ngx_cycle_t *cycle);    // 主进程退出回调函数

    uintptr_t             spare_hook0;    // 备用钩子函数0
    uintptr_t             spare_hook1;    // 备用钩子函数1
    uintptr_t             spare_hook2;    // 备用钩子函数2
    uintptr_t             spare_hook3;    // 备用钩子函数3
    uintptr_t             spare_hook4;    // 备用钩子函数4
    uintptr_t             spare_hook5;    // 备用钩子函数5
    uintptr_t             spare_hook6;    // 备用钩子函数6
    uintptr_t             spare_hook7;    // 备用钩子函数7
};



/*
 * 结构体 ngx_core_module_t 表示Nginx核心模块的基本信息结构
 * 该结构体用于描述Nginx核心模块的基本属性和回调函数
 */
typedef struct {
    ngx_str_t             name;                // 模块的名称
    void               *(*create_conf)(ngx_cycle_t *cycle);  // 创建模块配置结构的回调函数
    char               *(*init_conf)(ngx_cycle_t *cycle, void *conf);  // 初始化模块配置结构的回调函数
} ngx_core_module_t;

/*
 * ngx_preinit_modules函数用于预初始化Nginx的模块
 * 返回 NGX_OK 表示成功，NGX_ERROR 表示失败
 */
ngx_int_t ngx_preinit_modules(void);

/*
 * ngx_cycle_modules函数用于在Nginx运行周期中初始化模块
 * 返回 NGX_OK 表示成功，NGX_ERROR 表示失败
 */
ngx_int_t ngx_cycle_modules(ngx_cycle_t *cycle);

/*
 * ngx_init_modules函数用于在Nginx启动时初始化模块
 * 返回 NGX_OK 表示成功，NGX_ERROR 表示失败
 */
ngx_int_t ngx_init_modules(ngx_cycle_t *cycle);

/*
 * ngx_count_modules函数用于获取指定类型的模块数量
 * 返回模块数量
 */
ngx_int_t ngx_count_modules(ngx_cycle_t *cycle, ngx_uint_t type);

/*
 * ngx_add_module函数用于向Nginx添加模块
 * 参数：
 *   - cf: 配置结构
 *   - file: 模块对应的文件名
 *   - module: 模块结构体
 *   - order: 模块顺序
 * 返回 NGX_OK 表示成功，NGX_ERROR 表示失败
 */
ngx_int_t ngx_add_module(ngx_conf_t *cf, ngx_str_t *file, ngx_module_t *module, char **order);

/*
 * ngx_modules 数组存储所有已加载的模块
 */
extern ngx_module_t  *ngx_modules[];

/*
 * ngx_max_module 存储已加载的模块的最大数量
 */
extern ngx_uint_t     ngx_max_module;

/*
 * ngx_module_names 数组存储所有已加载模块的名称
 */
extern char          *ngx_module_names[];



#endif /* _NGX_MODULE_H_INCLUDED_ */
