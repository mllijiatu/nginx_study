
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

#define DEFAULT_CONNECTIONS 512

extern ngx_module_t ngx_kqueue_module;
extern ngx_module_t ngx_eventport_module;
extern ngx_module_t ngx_devpoll_module;
extern ngx_module_t ngx_epoll_module;
extern ngx_module_t ngx_select_module;

static char *ngx_event_init_conf(ngx_cycle_t *cycle, void *conf);
static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle);
static char *ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd,
                                   void *conf);
static char *ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd,
                                        void *conf);

static void *ngx_event_core_create_conf(ngx_cycle_t *cycle);
static char *ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_uint_t ngx_timer_resolution;
sig_atomic_t ngx_event_timer_alarm;

static ngx_uint_t ngx_event_max_module;

ngx_uint_t ngx_event_flags;
ngx_event_actions_t ngx_event_actions;

static ngx_atomic_t connection_counter = 1;
ngx_atomic_t *ngx_connection_counter = &connection_counter;

ngx_atomic_t *ngx_accept_mutex_ptr;
ngx_shmtx_t ngx_accept_mutex;
ngx_uint_t ngx_use_accept_mutex;
ngx_uint_t ngx_accept_events;
ngx_uint_t ngx_accept_mutex_held;
ngx_msec_t ngx_accept_mutex_delay;
ngx_int_t ngx_accept_disabled;
ngx_uint_t ngx_use_exclusive_accept;

#if (NGX_STAT_STUB)

static ngx_atomic_t ngx_stat_accepted0;
ngx_atomic_t *ngx_stat_accepted = &ngx_stat_accepted0;
static ngx_atomic_t ngx_stat_handled0;
ngx_atomic_t *ngx_stat_handled = &ngx_stat_handled0;
static ngx_atomic_t ngx_stat_requests0;
ngx_atomic_t *ngx_stat_requests = &ngx_stat_requests0;
static ngx_atomic_t ngx_stat_active0;
ngx_atomic_t *ngx_stat_active = &ngx_stat_active0;
static ngx_atomic_t ngx_stat_reading0;
ngx_atomic_t *ngx_stat_reading = &ngx_stat_reading0;
static ngx_atomic_t ngx_stat_writing0;
ngx_atomic_t *ngx_stat_writing = &ngx_stat_writing0;
static ngx_atomic_t ngx_stat_waiting0;
ngx_atomic_t *ngx_stat_waiting = &ngx_stat_waiting0;

#endif

/*
 * 函数: ngx_events_commands
 * -------------------------
 * 描述: 该函数定义了ngx_events模块的配置指令数组，用于配置events模块的参数。
 * 参数: 无
 * 返回: ngx_command_t数组，最后一个元素为ngx_null_command，用于标识数组结束。
 */
static ngx_command_t ngx_events_commands[] = {

    /* events指令，用于配置events模块 */
    {ngx_string("events"),
     NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
     ngx_events_block,
     0,
     0,
     NULL},

    /* 数组结束标志，必须以ngx_null_command结尾 */
    ngx_null_command};

/*
 * 函数: ngx_core_module_t ngx_events_module_ctx
 * -------------------------------------------
 * 描述: 定义了ngx_events模块的核心模块上下文，包括模块的名称、配置结构和初始化回调函数。
 * 参数: 无
 * 返回: ngx_core_module_t结构体，用于指定核心模块的上下文信息。
 */
static ngx_core_module_t ngx_events_module_ctx = {
    ngx_string("events"), /* 模块名称 */
    NULL,                 /* 预留给模块的初始化回调函数 */
    ngx_event_init_conf   /* 模块的配置结构初始化回调函数 */
};

/*
 * 变量: ngx_module_t ngx_events_module
 * ------------------------------------
 * 描述: 定义了ngx_events模块的模块信息，包括模块的版本、上下文、指令集等。
 * 参数: 无
 * 返回: ngx_module_t结构体，用于指定模块的基本信息。
 */
ngx_module_t ngx_events_module = {
    NGX_MODULE_V1,          /* 用于版本控制的宏 */
    &ngx_events_module_ctx, /* 模块上下文 */
    ngx_events_commands,    /* 模块配置指令集 */
    NGX_CORE_MODULE,        /* 模块类型，这里是核心模块 */
    NULL,                   /* init master */
    NULL,                   /* init module */
    NULL,                   /* init process */
    NULL,                   /* init thread */
    NULL,                   /* exit thread */
    NULL,                   /* exit process */
    NULL,                   /* exit master */
    NGX_MODULE_V1_PADDING   /* 用于版本控制的宏 */
};

/*
 * 变量: ngx_str_t event_core_name
 * ------------------------------
 * 描述: 定义了一个ngx_str_t类型的变量event_core_name，用于存储事件核心的名称字符串。
 * 参数: 无
 * 返回: ngx_str_t类型的变量，存储事件核心的名称。
 */
static ngx_str_t event_core_name = ngx_string("event_core");

/*
 * 函数: ngx_command_t ngx_event_core_commands
 * -------------------------------------------
 * 描述: 定义了ngx_event_core模块的配置指令数组，用于配置event_core模块的参数。
 * 参数: 无
 * 返回: ngx_command_t数组，最后一个元素为ngx_null_command，用于标识数组结束。
 */
static ngx_command_t ngx_event_core_commands[] = {

    /* worker_connections指令，配置工作进程的最大连接数 */
    {ngx_string("worker_connections"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_event_connections,
     0,
     0,
     NULL},

    /* use指令，配置事件模块的使用方式 */
    {ngx_string("use"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_event_use,
     0,
     0,
     NULL},

    /* multi_accept指令，配置是否开启多个连接的同时接受 */
    {ngx_string("multi_accept"),
     NGX_EVENT_CONF | NGX_CONF_FLAG,
     ngx_conf_set_flag_slot,
     0,
     offsetof(ngx_event_conf_t, multi_accept),
     NULL},

    /* accept_mutex指令，配置是否开启连接互斥锁 */
    {ngx_string("accept_mutex"),
     NGX_EVENT_CONF | NGX_CONF_FLAG,
     ngx_conf_set_flag_slot,
     0,
     offsetof(ngx_event_conf_t, accept_mutex),
     NULL},

    /* accept_mutex_delay指令，配置互斥锁延迟时间 */
    {ngx_string("accept_mutex_delay"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_msec_slot,
     0,
     offsetof(ngx_event_conf_t, accept_mutex_delay),
     NULL},

    /* debug_connection指令，配置是否开启调试连接 */
    {ngx_string("debug_connection"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_event_debug_connection,
     0,
     0,
     NULL},

    /* 数组结束标志，必须以ngx_null_command结尾 */
    ngx_null_command};

static ngx_event_module_t ngx_event_core_module_ctx = {
    &event_core_name,
    ngx_event_core_create_conf, /* create configuration */
    ngx_event_core_init_conf,   /* init configuration */

    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}};

/**
 * ngx_event_core_module 模块定义
 */
ngx_module_t ngx_event_core_module = {
    NGX_MODULE_V1,              /* 标识 ngx_module_t 结构的版本号 */
    &ngx_event_core_module_ctx, /* 模块上下文 */
    ngx_event_core_commands,    /* 模块指令集 */
    NGX_EVENT_MODULE,           /* 模块类型 */
    NULL,                       /* 初始化 master 过程 */
    ngx_event_module_init,      /* 初始化模块过程 */
    ngx_event_process_init,     /* 初始化进程过程 */
    NULL,                       /* 初始化线程过程 */
    NULL,                       /* 退出线程过程 */
    NULL,                       /* 退出进程过程 */
    NULL,                       /* 退出 master 过程 */
    NGX_MODULE_V1_PADDING       /* 填充字段 */
};

/*
 * 描述：处理事件和定时器。
 *
 * 参数：
 *   - cycle：指向ngx_cycle_t结构的指针，表示当前周期。
 *
 * 返回：此函数不返回任何值。
 */

void ngx_process_events_and_timers(ngx_cycle_t *cycle)
{
    ngx_uint_t flags;
    ngx_msec_t timer, delta;

    // 如果定时器分辨率开启
    if (ngx_timer_resolution)
    {
        timer = NGX_TIMER_INFINITE;
        flags = 0;
    }
    else
    {
        timer = ngx_event_find_timer();
        flags = NGX_UPDATE_TIME;

#if (NGX_WIN32)

        /* 在网络不活动的情况下处理来自主进程的信号 */

        if (timer == NGX_TIMER_INFINITE || timer > 500)
        {
            timer = 500;
        }

#endif
    }

    // 如果使用接受互斥锁
    /**
     * ngx_use_accept_mutex变量代表是否使用accept互斥体
     * 默认是使用，可以通过accept_mutex off;指令关闭；
     * accept mutex 的作用就是避免惊群，同时实现负载均衡
     */
    if (ngx_use_accept_mutex)
    {
        /**
         * 	ngx_accept_disabled = ngx_cycle->connection_n / 8 - ngx_cycle->free_connection_n;
         * 	当connection达到连接总数的7/8的时候，就不再处理新的连接accept事件，只处理当前连接的read事件
         * 	这个是比较简单的一种负载均衡方法
         */
        if (ngx_accept_disabled > 0)
        {
            ngx_accept_disabled--;
        }
        else
        {
            // 尝试获取接受互斥锁
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR)
            {
                return;
            }

            if (ngx_accept_mutex_held)
            {
                /**
                 * 给flags增加标记NGX_POST_EVENTS，这个标记作为处理时间核心函数ngx_process_events的一个参数，这个函数中所有事件将延后处理。
                 * accept事件都放到ngx_posted_accept_events链表中，
                 * epollin|epollout普通事件都放到ngx_posted_events链表中
                 **/
                flags |= NGX_POST_EVENTS;
            }
            else
            {
                // 如果定时器是无限的或者大于接受互斥锁延迟时间
                /**
                 * 1. 获取锁失败，意味着既不能让当前worker进程频繁的试图抢锁，也不能让它经过太长事件再去抢锁
                 * 2. 开启了timer_resolution时间精度，需要让ngx_process_change方法在没有新事件的时候至少等待ngx_accept_mutex_delay毫秒之后再去试图抢锁
                 * 3. 没有开启时间精度时，如果最近一个定时器事件的超时时间距离现在超过了ngx_accept_mutex_delay毫秒，也要把timer设置为ngx_accept_mutex_delay毫秒
                 * 4. 不能让ngx_process_change方法在没有新事件的时候等待的时间超过ngx_accept_mutex_delay，这会影响整个负载均衡机制
                 * 5. 如果拿到锁的进程能很快处理完accpet，而没拿到锁的一直在等待，容易造成进程忙的很忙，空的很空
                 */
                if (timer == NGX_TIMER_INFINITE || timer > ngx_accept_mutex_delay)
                {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }

    // 如果有延迟的事件，立即处理
    if (!ngx_queue_empty(&ngx_posted_next_events))
    {
        ngx_event_move_posted_next(cycle);
        timer = 0;
    }

    delta = ngx_current_msec;

    /**
     * 事件调度函数
     * 1. 当拿到锁，flags=NGX_POST_EVENTS的时候，不会直接处理事件，
     * 将accept事件放到ngx_posted_accept_events，read事件放到ngx_posted_events队列
     * 2. 当没有拿到锁，则处理的全部是read事件，直接进行回调函数处理
     * 参数：timer-epoll_wait超时时间  (ngx_accept_mutex_delay-延迟拿锁事件   NGX_TIMER_INFINITE-正常的epollwait等待事件)
     */
    (void)ngx_process_events(cycle, timer, flags);

    delta = ngx_current_msec - delta;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "timer delta: %M", delta);

    ngx_event_process_posted(cycle, &ngx_posted_accept_events);

    // 如果持有接受互斥锁，释放锁
    if (ngx_accept_mutex_held)
    {
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    ngx_event_expire_timers();

    ngx_event_process_posted(cycle, &ngx_posted_events);
}

{
    ngx_uint_t flags;
    ngx_msec_t timer, delta;

    if (ngx_timer_resolution)
    {
        timer = NGX_TIMER_INFINITE;
        flags = 0;
    }
    else
    {
        timer = ngx_event_find_timer();
        flags = NGX_UPDATE_TIME;

#if (NGX_WIN32)

        /* handle signals from master in case of network inactivity */

        if (timer == NGX_TIMER_INFINITE || timer > 500)
        {
            timer = 500;
        }

#endif
    }

    if (ngx_use_accept_mutex)
    {
        if (ngx_accept_disabled > 0)
        {
            ngx_accept_disabled--;
        }
        else
        {
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR)
            {
                return;
            }

            if (ngx_accept_mutex_held)
            {
                flags |= NGX_POST_EVENTS;
            }
            else
            {
                if (timer == NGX_TIMER_INFINITE || timer > ngx_accept_mutex_delay)
                {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }

    if (!ngx_queue_empty(&ngx_posted_next_events))
    {
        ngx_event_move_posted_next(cycle);
        timer = 0;
    }

    delta = ngx_current_msec;

    (void)ngx_process_events(cycle, timer, flags);

    delta = ngx_current_msec - delta;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "timer delta: %M", delta);

    ngx_event_process_posted(cycle, &ngx_posted_accept_events);

    if (ngx_accept_mutex_held)
    {
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    ngx_event_expire_timers();

    ngx_event_process_posted(cycle, &ngx_posted_events);
}

ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags)
{
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT)
    {

        /* kqueue, epoll */

        if (!rev->active && !rev->ready)
        {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;
    }
    else if (ngx_event_flags & NGX_USE_LEVEL_EVENT)
    {

        /* select, poll, /dev/poll */

        if (!rev->active && !rev->ready)
        {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->active && (rev->ready || (flags & NGX_CLOSE_EVENT)))
        {
            if (ngx_del_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT | flags) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }
    else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT)
    {

        /* event ports */

        if (!rev->active && !rev->ready)
        {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->oneshot && rev->ready)
        {
            if (ngx_del_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* iocp */

    return NGX_OK;
}

ngx_int_t
ngx_handle_write_event(ngx_event_t *wev, size_t lowat)
{
    ngx_connection_t *c;

    if (lowat)
    {
        c = wev->data;

        if (ngx_send_lowat(c, lowat) == NGX_ERROR)
        {
            return NGX_ERROR;
        }
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT)
    {

        /* kqueue, epoll */

        if (!wev->active && !wev->ready)
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT,
                              NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0)) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;
    }
    else if (ngx_event_flags & NGX_USE_LEVEL_EVENT)
    {

        /* select, poll, /dev/poll */

        if (!wev->active && !wev->ready)
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->active && wev->ready)
        {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }
    else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT)
    {

        /* event ports */

        if (!wev->active && !wev->ready)
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->oneshot && wev->ready)
        {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* iocp */

    return NGX_OK;
}

static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{
#if (NGX_HAVE_REUSEPORT)
    ngx_uint_t i;
    ngx_core_conf_t *ccf;
    ngx_listening_t *ls;
#endif

    if (ngx_get_conf(cycle->conf_ctx, ngx_events_module) == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "no \"events\" section in configuration");
        return NGX_CONF_ERROR;
    }

    if (cycle->connection_n < cycle->listening.nelts + 1)
    {

        /*
         * there should be at least one connection for each listening
         * socket, plus an additional connection for channel
         */

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "%ui worker_connections are not enough "
                      "for %ui listening sockets",
                      cycle->connection_n, cycle->listening.nelts);

        return NGX_CONF_ERROR;
    }

#if (NGX_HAVE_REUSEPORT)

    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (!ngx_test_config && ccf->master)
    {

        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++)
        {

            if (!ls[i].reuseport || ls[i].worker != 0)
            {
                continue;
            }

            if (ngx_clone_listening(cycle, &ls[i]) != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }

            /* cloning may change cycle->listening.elts */

            ls = cycle->listening.elts;
        }
    }

#endif

    return NGX_CONF_OK;
}

/*
 * 函数: ngx_int_t ngx_event_module_init
 * ------------------------------------
 * 描述: 事件模块初始化函数，用于在启动时初始化事件模块相关的参数和共享内存。
 * 参数:
 *   - ngx_cycle_t *cycle: 运行时的ngx_cycle_t结构体指针。
 * 返回: NGX_OK 表示初始化成功，NGX_ERROR 表示初始化失败。
 */
static ngx_int_t
ngx_event_module_init(ngx_cycle_t *cycle)
{
    /*
     * 变量: void ***cf
     * ----------------
     * 描述: 定义了一个指向指针数组的指针cf，用于获取事件模块配置结构体指针。
     * 参数: 无
     * 返回: 指向指针数组的指针，用于获取事件模块配置结构体指针。
     */
    void ***cf;

    /*
     * 变量: u_char *shared
     * ---------------------
     * 描述: 定义了一个指向字符数组的指针shared，用于指向共享内存的起始地址。
     * 参数: 无
     * 返回: 指向字符数组的指针，指向共享内存的起始地址。
     */
    u_char *shared;

    /*
     * 变量: size_t size, cl
     * ---------------------
     * 描述: 定义了两个size_t类型的变量size和cl，用于存储共享内存的大小和cache line size。
     * 参数: 无
     * 返回: 无
     */
    size_t size, cl;

    /*
     * 变量: ngx_shm_t shm
     * -------------------
     * 描述: 定义了一个ngx_shm_t类型的变量shm，用于存储共享内存的相关信息。
     * 参数: 无
     * 返回: 无
     */
    ngx_shm_t shm;

    /*
     * 变量: ngx_time_t *tp
     * ---------------------
     * 描述: 定义了一个指向ngx_time_t结构体的指针tp，用于获取当前时间。
     * 参数: 无
     * 返回: 指向ngx_time_t结构体的指针，指向当前时间。
     */
    ngx_time_t *tp;

    /*
     * 变量: ngx_core_conf_t *ccf
     * ---------------------------
     * 描述: 定义了一个指向ngx_core_conf_t结构体的指针ccf，用于获取核心模块配置结构体指针。
     * 参数: 无
     * 返回: 指向ngx_core_conf_t结构体的指针，用于获取核心模块配置结构体指针。
     */
    ngx_core_conf_t *ccf;

    /*
     * 变量: ngx_event_conf_t *ecf
     * ----------------------------
     * 描述: 定义了一个指向ngx_event_conf_t结构体的指针ecf，用于获取事件模块配置结构体指针。
     * 参数: 无
     * 返回: 指向ngx_event_conf_t结构体的指针，用于获取事件模块配置结构体指针。
     */
    ngx_event_conf_t *ecf;

    /* 获取事件模块配置结构体指针 */
    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);
    ecf = (*cf)[ngx_event_core_module.ctx_index];

    /* 如果不是测试配置且当前进程是主进程或者单一进程，则记录事件模块的使用方式 */
    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER)
    {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "using the \"%s\" event method", ecf->name);
    }

    /* 获取核心模块配置结构体指针，用于获取timer_resolution参数 */
    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_timer_resolution = ccf->timer_resolution;

#if !(NGX_WIN32)
    {
        ngx_int_t limit;
        struct rlimit rlmt;

        /* 检查系统文件描述符限制，并进行相应的日志记录 */
        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) failed, ignored");
        }
        else
        {
            if (ecf->connections > (ngx_uint_t)rlmt.rlim_cur && (ccf->rlimit_nofile == NGX_CONF_UNSET || ecf->connections > (ngx_uint_t)ccf->rlimit_nofile))
            {
                limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ? (ngx_int_t)rlmt.rlim_cur : ccf->rlimit_nofile;

                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                              "%ui worker_connections exceed "
                              "open file resource limit: %i",
                              ecf->connections, limit);
            }
        }
    }
#endif /* !(NGX_WIN32) */

    /* 如果不是主进程，直接返回成功 */
    if (ccf->master == 0)
    {
        return NGX_OK;
    }

    /* 如果ngx_accept_mutex_ptr已经存在，说明已经初始化过，直接返回成功 */
    if (ngx_accept_mutex_ptr)
    {
        return NGX_OK;
    }

    /* cl应该大于等于cache line size，这里设置为128 */
    cl = 128;

    /* 计算共享内存大小 */
    size = cl    /* ngx_accept_mutex */
           + cl  /* ngx_connection_counter */
           + cl; /* ngx_temp_number */

#if (NGX_STAT_STUB)

    size += cl    /* ngx_stat_accepted */
            + cl  /* ngx_stat_handled */
            + cl  /* ngx_stat_requests */
            + cl  /* ngx_stat_active */
            + cl  /* ngx_stat_reading */
            + cl  /* ngx_stat_writing */
            + cl; /* ngx_stat_waiting */

#endif

    /* 设置共享内存结构体参数 */
    shm.size = size;
    ngx_str_set(&shm.name, "nginx_shared_zone");
    shm.log = cycle->log;

    /* 分配共享内存 */
    if (ngx_shm_alloc(&shm) != NGX_OK)
    {
        return NGX_ERROR;
    }

    shared = shm.addr;

    /* 初始化ngx_accept_mutex指针和相关参数 */
    ngx_accept_mutex_ptr = (ngx_atomic_t *)shared;
    ngx_accept_mutex.spin = (ngx_uint_t)-1;

    /* 创建共享内存互斥锁 */
    if (ngx_shmtx_create(&ngx_accept_mutex, (ngx_shmtx_sh_t *)shared,
                         cycle->lock_file.data) != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* 初始化连接计数器 */
    ngx_connection_counter = (ngx_atomic_t *)(shared + 1 * cl);
    (void)ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "counter: %p, %uA",
                   ngx_connection_counter, *ngx_connection_counter);

    /* 初始化临时数值 */
    ngx_temp_number = (ngx_atomic_t *)(shared + 2 * cl);

    /* 获取当前时间并生成随机数作为ngx_random_number */
    tp = ngx_timeofday();
    ngx_random_number = (tp->msec << 16) + ngx_pid;

#if (NGX_STAT_STUB)

    /* 初始化统计变量（仅在调试模式下有效） */
    ngx_stat_accepted = (ngx_atomic_t *)(shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *)(shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *)(shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *)(shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *)(shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *)(shared + 8 * cl);
    ngx_stat_waiting = (ngx_atomic_t *)(shared + 9 * cl);

#endif

    return NGX_OK;
}

{
    void ***cf;
    u_char *shared;
    size_t size, cl;
    ngx_shm_t shm;
    ngx_time_t *tp;
    ngx_core_conf_t *ccf;
    ngx_event_conf_t *ecf;

    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);
    ecf = (*cf)[ngx_event_core_module.ctx_index];

    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER)
    {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "using the \"%s\" event method", ecf->name);
    }

    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_timer_resolution = ccf->timer_resolution;

#if !(NGX_WIN32)
    {
        ngx_int_t limit;
        struct rlimit rlmt;

        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) failed, ignored");
        }
        else
        {
            if (ecf->connections > (ngx_uint_t)rlmt.rlim_cur && (ccf->rlimit_nofile == NGX_CONF_UNSET || ecf->connections > (ngx_uint_t)ccf->rlimit_nofile))
            {
                limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ? (ngx_int_t)rlmt.rlim_cur : ccf->rlimit_nofile;

                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                              "%ui worker_connections exceed "
                              "open file resource limit: %i",
                              ecf->connections, limit);
            }
        }
    }
#endif /* !(NGX_WIN32) */

    if (ccf->master == 0)
    {
        return NGX_OK;
    }

    if (ngx_accept_mutex_ptr)
    {
        return NGX_OK;
    }

    /* cl should be equal to or greater than cache line size */

    cl = 128;

    size = cl    /* ngx_accept_mutex */
           + cl  /* ngx_connection_counter */
           + cl; /* ngx_temp_number */

#if (NGX_STAT_STUB)

    size += cl    /* ngx_stat_accepted */
            + cl  /* ngx_stat_handled */
            + cl  /* ngx_stat_requests */
            + cl  /* ngx_stat_active */
            + cl  /* ngx_stat_reading */
            + cl  /* ngx_stat_writing */
            + cl; /* ngx_stat_waiting */

#endif

    shm.size = size;
    ngx_str_set(&shm.name, "nginx_shared_zone");
    shm.log = cycle->log;

    if (ngx_shm_alloc(&shm) != NGX_OK)
    {
        return NGX_ERROR;
    }

    shared = shm.addr;

    ngx_accept_mutex_ptr = (ngx_atomic_t *)shared;
    ngx_accept_mutex.spin = (ngx_uint_t)-1;

    if (ngx_shmtx_create(&ngx_accept_mutex, (ngx_shmtx_sh_t *)shared,
                         cycle->lock_file.data) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_connection_counter = (ngx_atomic_t *)(shared + 1 * cl);

    (void)ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "counter: %p, %uA",
                   ngx_connection_counter, *ngx_connection_counter);

    ngx_temp_number = (ngx_atomic_t *)(shared + 2 * cl);

    tp = ngx_timeofday();

    ngx_random_number = (tp->msec << 16) + ngx_pid;

#if (NGX_STAT_STUB)

    ngx_stat_accepted = (ngx_atomic_t *)(shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *)(shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *)(shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *)(shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *)(shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *)(shared + 8 * cl);
    ngx_stat_waiting = (ngx_atomic_t *)(shared + 9 * cl);

#endif

    return NGX_OK;
}

#if !(NGX_WIN32)

/*
 * 定时器信号处理函数，当定时器触发时被调用。
 * 参数 signo 表示触发信号的类型。
 */
static void
ngx_timer_signal_handler(int signo)
{
    // 设置全局标志 ngx_event_timer_alarm 为1，表示定时器已触发
    ngx_event_timer_alarm = 1;

    // 调试日志，记录定时器信号触发的事件，可选编译开关控制是否打印
#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif

/**
 * 初始化当前循环的事件处理。
 *
 * @param cycle 当前循环的 ngx_cycle_t 结构。
 *
 * @return 如果初始化成功则返回 NGX_OK，否则返回 NGX_ERROR。
 */
static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{
    /**
     * 定义一些在 ngx_event_process_init 函数中会使用的变量。
     */
    ngx_uint_t m, i;                  /* 循环变量 */
    ngx_event_t *rev, *wev;           /* 读事件和写事件指针 */
    ngx_listening_t *ls;              /* 监听套接字结构指针 */
    ngx_connection_t *c, *next, *old; /* 连接结构指针 */
    ngx_core_conf_t *ccf;             /* 核心配置结构指针 */
    ngx_event_conf_t *ecf;            /* 事件模块配置结构指针 */
    ngx_event_module_t *module;       /* 事件模块指针 */

    // 获取核心配置和事件核心配置
    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);

    // 根据配置初始化接收互斥锁设置
    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex)
    {
        ngx_use_accept_mutex = 1;
        ngx_accept_mutex_held = 0;
        ngx_accept_mutex_delay = ecf->accept_mutex_delay;
    }
    else
    {
        ngx_use_accept_mutex = 0;
    }

#if (NGX_WIN32)
    // 在 Win32 上禁用接收互斥锁，以免引发死锁问题
    ngx_use_accept_mutex = 0;

#endif

    ngx_use_exclusive_accept = 0;

    // 初始化用于已发布事件的队列
    ngx_queue_init(&ngx_posted_accept_events);
    ngx_queue_init(&ngx_posted_next_events);
    ngx_queue_init(&ngx_posted_events);

    // 初始化事件计时器
    if (ngx_event_timer_init(cycle->log) == NGX_ERROR)
    {
        return NGX_ERROR;
    }

    // 遍历模块以找到选择的事件模块
    for (m = 0; cycle->modules[m]; m++)
    {
        if (cycle->modules[m]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        if (cycle->modules[m]->ctx_index != ecf->use)
        {
            continue;
        }

        module = cycle->modules[m]->ctx;

        // 初始化选择的事件模块
        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK)
        {
            /* 致命错误 */
            exit(2);
        }

        break;
    }

    /*
     * 在非 Win32 系统上，如果定义了 ngx_timer_resolution 并且未使用定时器事件，则配置定时器分辨率和信号处理程序。
     */
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT))
    {
        // 定义 sigaction 结构体用于设置信号处理程序
        struct sigaction sa;
        // 定义 itimerval 结构体用于设置定时器的时间间隔
        struct itimerval itv;

        // 清空 sa 结构体，并设置信号处理函数为 ngx_timer_signal_handler
        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);

        // 设置 SIGALRM 信号的处理程序为 ngx_timer_signal_handler
        if (sigaction(SIGALRM, &sa, NULL) == -1)
        {
            // 如果设置失败，则记录错误日志并返回 NGX_ERROR
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) 失败");
            return NGX_ERROR;
        }

        // 设置定时器的时间间隔，it_interval 为定时器的重复间隔，it_value 为定时器第一次触发的时间
        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000) * 1000;

        // 设置定时器，使用 ITIMER_REAL 表示实时定时器
        if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
        {
            // 如果设置失败，则记录错误日志
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setitimer() 失败");
        }
    }

    /*
     * 如果使用文件描述符事件，则获取系统资源限制并分配相应的内存。
     */
    if (ngx_event_flags & NGX_USE_FD_EVENT)
    {
        // 定义结构体 rlimit 用于存储系统资源限制信息
        struct rlimit rlmt;

        // 获取系统对文件描述符的限制信息
        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1)
        {
            // 获取失败时记录错误日志并返回 NGX_ERROR
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) 失败");
            return NGX_ERROR;
        }

        // 将系统限制的文件描述符数设置为当前进程的文件描述符数
        cycle->files_n = (ngx_uint_t)rlmt.rlim_cur;

        // 分配内存以存储 ngx_connection_t 指针的数组，大小为文件描述符数
        cycle->files = ngx_calloc(sizeof(ngx_connection_t *) * cycle->files_n,
                                  cycle->log);

        // 分配内存失败时返回 NGX_ERROR
        if (cycle->files == NULL)
        {
            return NGX_ERROR;
        }
    }

#else
    // 在 Win32 上，处理定时器分辨率配置
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT))
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "the \"timer_resolution\" 指令在配置的事件方法中不受支持，已忽略");
        ngx_timer_resolution = 0;
    }

#endif

    // 分配连接数组、读事件数组和写事件数组的内存
    cycle->connections =
        ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);
    if (cycle->connections == NULL)
    {
        return NGX_ERROR;
    }

    c = cycle->connections;

    cycle->read_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,
                                   cycle->log);
    if (cycle->read_events == NULL)
    {
        return NGX_ERROR;
    }

    rev = cycle->read_events;
    for (i = 0; i < cycle->connection_n; i++)
    {
        rev[i].closed = 1;
        rev[i].instance = 1;
    }

    cycle->write_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,
                                    cycle->log);
    if (cycle->write_events == NULL)
    {
        return NGX_ERROR;
    }

    wev = cycle->write_events;
    for (i = 0; i < cycle->connection_n; i++)
    {
        wev[i].closed = 1;
    }

    i = cycle->connection_n;
    next = NULL;

    // 为每个连接初始化连接和事件
    do
    {
        i--;

        c[i].data = next;
        c[i].read = &cycle->read_events[i];
        c[i].write = &cycle->write_events[i];
        c[i].fd = (ngx_socket_t)-1;

        next = &c[i];
    } while (i);

    cycle->free_connections = next;
    cycle->free_connection_n = cycle->connection_n;

    // 处理每个监听套接字
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++)
    {
#if (NGX_HAVE_REUSEPORT)
        // 如果使用 reuseport，则跳过从其他 worker 复用的监听套接字
        if (ls[i].reuseport && ls[i].worker != ngx_worker)
        {
            continue;
        }
#endif

        c = ngx_get_connection(ls[i].fd, cycle->log);

        if (c == NULL)
        {
            return NGX_ERROR;
        }

        c->type = ls[i].type;
        c->log = &ls[i].log;

        c->listening = &ls[i];
        ls[i].connection = c;

        rev = c->read;

        rev->log = c->log;
        rev->accept = 1;

#if (NGX_HAVE_DEFERRED_ACCEPT)
        rev->deferred_accept = ls[i].deferred_accept;
#endif

        // 根据操作系统处理事件
        if (!(ngx_event_flags & NGX_USE_IOCP_EVENT) && cycle->old_cycle)
        {
            if (ls[i].previous)
            {
                /*
                 * 删除与旧循环读事件数组绑定的旧的接受事件
                 */

                old = ls[i].previous->connection;

                if (ngx_del_event(old->read, NGX_READ_EVENT, NGX_CLOSE_EVENT) == NGX_ERROR)
                {
                    return NGX_ERROR;
                }

                old->fd = (ngx_socket_t)-1;
            }
        }

#if (NGX_WIN32)
        // 处理 Win32 特定的事件配置
        if (ngx_event_flags & NGX_USE_IOCP_EVENT)
        {
            ngx_iocp_conf_t *iocpcf;

            rev->handler = ngx_event_acceptex;

            if (ngx_use_accept_mutex)
            {
                continue;
            }

            if (ngx_add_event(rev, 0, NGX_IOCP_ACCEPT)
            {
                return NGX_ERROR;
            }

            ls[i].log.handler = ngx_acceptex_log_error;

            iocpcf = ngx_event_get_conf(cycle->conf_ctx, ngx_iocp_module);
            if (ngx_event_post_acceptex(&ls[i], iocpcf->post_acceptex) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
        else
        {
            rev->handler = ngx_event_accept;

            if (ngx_use_accept_mutex)
            {
                continue;
            }

            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

#else

        rev->handler = (c->type == SOCK_STREAM) ? ngx_event_accept
                                                : ngx_event_recvmsg;

#if (NGX_HAVE_REUSEPORT)

        if (ls[i].reuseport)
        {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            continue;
        }

#endif

        if (ngx_use_accept_mutex)
        {
            continue;
        }

#if (NGX_HAVE_EPOLLEXCLUSIVE)

        if ((ngx_event_flags & NGX_USE_EPOLL_EVENT) && ccf->worker_processes > 1)
        {
            ngx_use_exclusive_accept = 1;

            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_EXCLUSIVE_EVENT) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            continue;
        }

#endif

        if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR)
        {
            return NGX_ERROR;
        }

#endif
    }

    return NGX_OK;
}

ngx_int_t
ngx_send_lowat(ngx_connection_t *c, size_t lowat)
{
    int sndlowat;

#if (NGX_HAVE_LOWAT_EVENT)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT)
    {
        c->write->available = lowat;
        return NGX_OK;
    }

#endif

    if (lowat == 0 || c->sndlowat)
    {
        return NGX_OK;
    }

    sndlowat = (int)lowat;

    if (setsockopt(c->fd, SOL_SOCKET, SO_SNDLOWAT,
                   (const void *)&sndlowat, sizeof(int)) == -1)
    {
        ngx_connection_error(c, ngx_socket_errno,
                             "setsockopt(SO_SNDLOWAT) failed");
        return NGX_ERROR;
    }

    c->sndlowat = 1;

    return NGX_OK;
}

/*
 * 函数: ngx_events_block
 * ---------------------
 * 描述: 解析配置文件中的 "events" 块，并初始化事件模块的配置。
 * 参数:
 *   - ngx_conf_t *cf: ngx_conf_t结构体指针，包含了配置解析的相关信息。
 *   - ngx_command_t *cmd: ngx_command_t结构体指针，表示解析的命令。
 *   - void *conf: 指向事件模块配置的指针，该指针将被设置为一个包含所有事件模块配置的数组。
 * 返回: NGX_CONF_OK 表示成功，其他值表示失败。
 */
static char *
ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *rv;
    void ***ctx;
    ngx_uint_t i;
    ngx_conf_t pcf;
    ngx_event_module_t *m;

    /* 检查是否已经解析过 "events" 块，防止重复解析 */
    if (*(void **)conf)
    {
        return "is duplicate";
    }

    /* 统计事件模块的数量，并设置它们的索引 */
    ngx_event_max_module = ngx_count_modules(cf->cycle, NGX_EVENT_MODULE);

    /* 分配内存存储事件模块的配置信息 */
    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL)
    {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));
    if (*ctx == NULL)
    {
        return NGX_CONF_ERROR;
    }

    *(void **)conf = ctx;

    /* 遍历事件模块，调用每个模块的 create_conf 方法创建模块的配置结构体 */
    for (i = 0; cf->cycle->modules[i]; i++)
    {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->create_conf)
        {
            (*ctx)[cf->cycle->modules[i]->ctx_index] =
                m->create_conf(cf->cycle);
            if ((*ctx)[cf->cycle->modules[i]->ctx_index] == NULL)
            {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;

    /* 解析 "events" 块 */
    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK)
    {
        return rv;
    }

    /* 调用每个事件模块的 init_conf 方法初始化配置 */
    for (i = 0; cf->cycle->modules[i]; i++)
    {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->init_conf)
        {
            rv = m->init_conf(cf->cycle,
                              (*ctx)[cf->cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK)
            {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}

{
    char *rv;
    void ***ctx;
    ngx_uint_t i;
    ngx_conf_t pcf;
    ngx_event_module_t *m;

    if (*(void **)conf)
    {
        return "is duplicate";
    }

    /* count the number of the event modules and set up their indices */

    ngx_event_max_module = ngx_count_modules(cf->cycle, NGX_EVENT_MODULE);

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL)
    {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));
    if (*ctx == NULL)
    {
        return NGX_CONF_ERROR;
    }

    *(void **)conf = ctx;

    for (i = 0; cf->cycle->modules[i]; i++)
    {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->create_conf)
        {
            (*ctx)[cf->cycle->modules[i]->ctx_index] =
                m->create_conf(cf->cycle);
            if ((*ctx)[cf->cycle->modules[i]->ctx_index] == NULL)
            {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK)
    {
        return rv;
    }

    for (i = 0; cf->cycle->modules[i]; i++)
    {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->init_conf)
        {
            rv = m->init_conf(cf->cycle,
                              (*ctx)[cf->cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK)
            {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t *ecf = conf;

    ngx_str_t *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT)
    {
        return "is duplicate";
    }

    value = cf->args->elts;
    ecf->connections = ngx_atoi(value[1].data, value[1].len);
    if (ecf->connections == (ngx_uint_t)NGX_ERROR)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number \"%V\"", &value[1]);

        return NGX_CONF_ERROR;
    }

    cf->cycle->connection_n = ecf->connections;

    return NGX_CONF_OK;
}

static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t *ecf = conf;

    ngx_int_t m;
    ngx_str_t *value;
    ngx_event_conf_t *old_ecf;
    ngx_event_module_t *module;

    if (ecf->use != NGX_CONF_UNSET_UINT)
    {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (cf->cycle->old_cycle->conf_ctx)
    {
        old_ecf = ngx_event_get_conf(cf->cycle->old_cycle->conf_ctx,
                                     ngx_event_core_module);
    }
    else
    {
        old_ecf = NULL;
    }

    for (m = 0; cf->cycle->modules[m]; m++)
    {
        if (cf->cycle->modules[m]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;
        if (module->name->len == value[1].len)
        {
            if (ngx_strcmp(module->name->data, value[1].data) == 0)
            {
                ecf->use = cf->cycle->modules[m]->ctx_index;
                ecf->name = module->name->data;

                if (ngx_process == NGX_PROCESS_SINGLE && old_ecf && old_ecf->use != ecf->use)
                {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "when the server runs without a master process "
                                       "the \"%V\" event type must be the same as "
                                       "in previous configuration - \"%s\" "
                                       "and it cannot be changed on the fly, "
                                       "to change it you need to stop server "
                                       "and start it again",
                                       &value[1], old_ecf->name);

                    return NGX_CONF_ERROR;
                }

                return NGX_CONF_OK;
            }
        }
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid event type \"%V\"", &value[1]);

    return NGX_CONF_ERROR;
}

static char *
ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_DEBUG)
    ngx_event_conf_t *ecf = conf;

    ngx_int_t rc;
    ngx_str_t *value;
    ngx_url_t u;
    ngx_cidr_t c, *cidr;
    ngx_uint_t i;
    struct sockaddr_in *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6 *sin6;
#endif

    value = cf->args->elts;

#if (NGX_HAVE_UNIX_DOMAIN)

    if (ngx_strcmp(value[1].data, "unix:") == 0)
    {
        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL)
        {
            return NGX_CONF_ERROR;
        }

        cidr->family = AF_UNIX;
        return NGX_CONF_OK;
    }

#endif

    rc = ngx_ptocidr(&value[1], &c);

    if (rc != NGX_ERROR)
    {
        if (rc == NGX_DONE)
        {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "low address bits of %V are meaningless",
                               &value[1]);
        }

        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL)
        {
            return NGX_CONF_ERROR;
        }

        *cidr = c;

        return NGX_CONF_OK;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));
    u.host = value[1];

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK)
    {
        if (u.err)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "%s in debug_connection \"%V\"",
                               u.err, &u.host);
        }

        return NGX_CONF_ERROR;
    }

    cidr = ngx_array_push_n(&ecf->debug_connection, u.naddrs);
    if (cidr == NULL)
    {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(cidr, u.naddrs * sizeof(ngx_cidr_t));

    for (i = 0; i < u.naddrs; i++)
    {
        cidr[i].family = u.addrs[i].sockaddr->sa_family;

        switch (cidr[i].family)
        {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            sin6 = (struct sockaddr_in6 *)u.addrs[i].sockaddr;
            cidr[i].u.in6.addr = sin6->sin6_addr;
            ngx_memset(cidr[i].u.in6.mask.s6_addr, 0xff, 16);
            break;
#endif

        default: /* AF_INET */
            sin = (struct sockaddr_in *)u.addrs[i].sockaddr;
            cidr[i].u.in.addr = sin->sin_addr.s_addr;
            cidr[i].u.in.mask = 0xffffffff;
            break;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"debug_connection\" is ignored, you need to rebuild "
                       "nginx using --with-debug option to enable it");

#endif

    return NGX_CONF_OK;
}

/*
 * 函数: ngx_event_core_create_conf
 * -------------------------------
 * 描述: 创建事件核心模块的配置结构体，并初始化默认值。
 * 参数:
 *   - ngx_cycle_t *cycle: ngx_cycle_t结构体指针，表示当前运行的Nginx周期。
 * 返回: 创建的事件核心模块配置结构体指针，如果内存分配失败则返回NULL。
 */
static void *
ngx_event_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_event_conf_t *ecf;

    /* 分配内存用于存储事件核心模块的配置结构体 */
    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL)
    {
        return NULL;
    }

    /* 初始化默认值 */
    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *)NGX_CONF_UNSET;

#if (NGX_DEBUG)

    /* 在调试模式下，初始化调试连接数组 */
    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4,
                       sizeof(ngx_cidr_t)) == NGX_ERROR)
    {
        return NULL;
    }

#endif

    return ecf;
}

{
    ngx_event_conf_t *ecf;

    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL)
    {
        return NULL;
    }

    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *)NGX_CONF_UNSET;

#if (NGX_DEBUG)

    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4,
                       sizeof(ngx_cidr_t)) == NGX_ERROR)
    {
        return NULL;
    }

#endif

    return ecf;
}

/*
 * 函数: ngx_event_core_init_conf
 * -----------------------------
 * 描述: 初始化事件核心模块的配置结构体。
 * 参数:
 *   - ngx_cycle_t *cycle: ngx_cycle_t结构体指针，表示当前运行的Nginx周期。
 *   - void *conf: 指向事件核心模块配置结构体的指针。
 * 返回: NGX_CONF_OK 表示成功，NGX_CONF_ERROR 表示失败。
 */
static char *
ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t *ecf = conf;
    ngx_module_t *module;
    ngx_event_module_t *event_module;
    ngx_int_t i;

    module = NULL;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)

    /* 检测是否支持 epoll 模块 */
    int fd = epoll_create(100);

    if (fd != -1)
    {
        (void)close(fd);
        module = &ngx_epoll_module;
    }
    else if (ngx_errno != NGX_ENOSYS)
    {
        module = &ngx_epoll_module;
    }

#endif

#if (NGX_HAVE_DEVPOLL) && !(NGX_TEST_BUILD_DEVPOLL)

    /* 检测是否支持 devpoll 模块 */
    module = &ngx_devpoll_module;

#endif

#if (NGX_HAVE_KQUEUE)

    /* 检测是否支持 kqueue 模块 */
    module = &ngx_kqueue_module;

#endif

#if (NGX_HAVE_SELECT)

    /* 检测是否支持 select 模块 */
    if (module == NULL)
    {
        module = &ngx_select_module;
    }

#endif

    /* 如果以上模块都不可用，则循环查找第一个 NGX_EVENT_MODULE 类型的模块 */
    if (module == NULL)
    {
        for (i = 0; cycle->modules[i]; i++)
        {
            if (cycle->modules[i]->type != NGX_EVENT_MODULE)
            {
                continue;
            }

            event_module = cycle->modules[i]->ctx;

            /* 跳过事件核心模块 */
            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0)
            {
                continue;
            }

            module = cycle->modules[i];
            break;
        }
    }

    /* 检查是否找到合适的事件模块 */
    if (module == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }

    /* 初始化事件核心模块配置结构体的默认值 */
    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);
    cycle->connection_n = ecf->connections;

    ngx_conf_init_uint_value(ecf->use, module->ctx_index);

    event_module = module->ctx;
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 0);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);

    return NGX_CONF_OK;
}

ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t *ecf = conf;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    int fd;
#endif
    ngx_int_t i;
    ngx_module_t *module;
    ngx_event_module_t *event_module;

    module = NULL;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)

    fd = epoll_create(100);

    if (fd != -1)
    {
        (void)close(fd);
        module = &ngx_epoll_module;
    }
    else if (ngx_errno != NGX_ENOSYS)
    {
        module = &ngx_epoll_module;
    }

#endif

#if (NGX_HAVE_DEVPOLL) && !(NGX_TEST_BUILD_DEVPOLL)

    module = &ngx_devpoll_module;

#endif

#if (NGX_HAVE_KQUEUE)

    module = &ngx_kqueue_module;

#endif

#if (NGX_HAVE_SELECT)

    if (module == NULL)
    {
        module = &ngx_select_module;
    }

#endif

    if (module == NULL)
    {
        for (i = 0; cycle->modules[i]; i++)
        {

            if (cycle->modules[i]->type != NGX_EVENT_MODULE)
            {
                continue;
            }

            event_module = cycle->modules[i]->ctx;

            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0)
            {
                continue;
            }

            module = cycle->modules[i];
            break;
        }
    }

    if (module == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }

    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);
    cycle->connection_n = ecf->connections;

    ngx_conf_init_uint_value(ecf->use, module->ctx_index);

    event_module = module->ctx;
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 0);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);

    return NGX_CONF_OK;
}
