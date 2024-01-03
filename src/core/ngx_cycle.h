
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     NGX_DEFAULT_POOL_SIZE
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);

struct ngx_shm_zone_s {
    void                     *data;
    ngx_shm_t                 shm;
    ngx_shm_zone_init_pt      init;
    void                     *tag;
    void                     *sync;
    ngx_uint_t                noreuse;  /* unsigned  noreuse:1; */
};


/**
 * @brief NGX的全局cycle结构体，包含了NGINX运行时的各种配置和状态信息
 */
struct ngx_cycle_s {
    void                  ****conf_ctx;                    /**< 配置上下文数组，包含所有模块的配置信息 */
    ngx_pool_t               *pool;                         /**< 内存池，用于分配内存 */

    ngx_log_t                *log;                          /**< 日志结构体指针，记录NGINX的日志信息 */
    ngx_log_t                 new_log;                      /**< 新的日志结构体，用于重新初始化日志 */

    ngx_uint_t                log_use_stderr;               /**< 是否将日志输出到标准错误流，1表示是，0表示否 */

    ngx_connection_t        **files;                        /**< 连接文件数组，记录打开的文件描述符 */
    ngx_connection_t         *free_connections;             /**< 空闲连接链表头指针 */
    ngx_uint_t                free_connection_n;            /**< 空闲连接个数 */

    ngx_module_t            **modules;                      /**< 模块数组，记录所有已加载的模块 */
    ngx_uint_t                modules_n;                    /**< 模块数量 */
    ngx_uint_t                modules_used;                 /**< 是否使用了模块，1表示是，0表示否 */

    ngx_queue_t               reusable_connections_queue;   /**< 可重用连接的队列 */
    ngx_uint_t                reusable_connections_n;       /**< 可重用连接个数 */
    time_t                    connections_reuse_time;       /**< 连接可重用的时间阈值 */

    ngx_array_t               listening;                    /**< 监听套接字数组 */
    ngx_array_t               paths;                        /**< 路径数组，存储NGINX相关的路径信息 */

    ngx_array_t               config_dump;                  /**< 配置信息转储数组 */
    ngx_rbtree_t              config_dump_rbtree;           /**< 配置信息转储的红黑树 */
    ngx_rbtree_node_t         config_dump_sentinel;         /**< 配置信息转储的红黑树哨兵节点 */

    ngx_list_t                open_files;                   /**< 打开的文件链表 */
    ngx_list_t                shared_memory;                /**< 共享内存链表 */

    ngx_uint_t                connection_n;                 /**< 连接个数 */
    ngx_uint_t                files_n;                      /**< 文件个数 */

    ngx_connection_t         *connections;                  /**< 连接数组 */
    ngx_event_t              *read_events;                  /**< 读事件数组 */
    ngx_event_t              *write_events;                 /**< 写事件数组 */

    ngx_cycle_t              *old_cycle;                   /**< 旧的cycle结构体，用于NGINX的平滑升级 */

    ngx_str_t                 conf_file;                    /**< 配置文件路径 */
    ngx_str_t                 conf_param;                   /**< 配置参数 */
    ngx_str_t                 conf_prefix;                  /**< 配置文件路径前缀 */
    ngx_str_t                 prefix;                       /**< NGINX安装路径前缀 */
    ngx_str_t                 error_log;                    /**< 错误日志文件路径 */
    ngx_str_t                 lock_file;                    /**< 进程锁文件路径 */
    ngx_str_t                 hostname;                     /**< 主机名 */
};



/**
 * @brief NGX核心模块的配置结构体
 */
typedef struct {
    ngx_flag_t                daemon;               /**< 是否以守护进程方式运行 */
    ngx_flag_t                master;               /**< 是否以master进程方式运行 */

    ngx_msec_t                timer_resolution;     /**< 定时器分辨率，单位毫秒 */
    ngx_msec_t                shutdown_timeout;     /**< 关闭超时时间，单位毫秒 */

    ngx_int_t                 worker_processes;     /**< worker进程数 */
    ngx_int_t                 debug_points;         /**< 调试点 */

    ngx_int_t                 rlimit_nofile;        /**< 文件描述符限制 */
    off_t                     rlimit_core;          /**< core文件大小限制 */

    int                       priority;             /**< 进程优先级 */

    ngx_uint_t                cpu_affinity_auto;    /**< CPU亲和性是否自动设置 */
    ngx_uint_t                cpu_affinity_n;       /**< CPU亲和性的CPU个数 */
    ngx_cpuset_t             *cpu_affinity;         /**< CPU亲和性的CPU集合 */

    char                     *username;             /**< 运行进程的用户名 */
    ngx_uid_t                 user;                 /**< 运行进程的用户ID */
    ngx_gid_t                 group;                /**< 运行进程的组ID */

    ngx_str_t                 working_directory;    /**< 工作目录 */
    ngx_str_t                 lock_file;            /**< 进程锁文件路径 */

    ngx_str_t                 pid;                  /**< PID文件路径 */
    ngx_str_t                 oldpid;               /**< 旧的PID文件路径 */

    ngx_array_t               env;                  /**< 环境变量数组 */
    char                    **environment;          /**< 环境变量指针数组 */

    ngx_uint_t                transparent;          /**< 是否开启透明模式，1表示是，0表示否 */
} ngx_core_conf_t;



#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);
ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);
void ngx_delete_pidfile(ngx_cycle_t *cycle);
ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);
void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);
char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);
ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
ngx_cpuset_t *ngx_get_cpu_affinity(ngx_uint_t n);
ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
    size_t size, void *tag);
void ngx_set_shutdown_timer(ngx_cycle_t *cycle);


extern volatile ngx_cycle_t  *ngx_cycle;
extern ngx_array_t            ngx_old_cycles;
extern ngx_module_t           ngx_core_module;
extern ngx_uint_t             ngx_test_config;
extern ngx_uint_t             ngx_dump_config;
extern ngx_uint_t             ngx_quiet_mode;


#endif /* _NGX_CYCLE_H_INCLUDED_ */
