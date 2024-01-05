
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

#if (NGX_TEST_BUILD_EPOLL)

/* epoll declarations */

#define EPOLLIN 0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDNORM 0x040
#define EPOLLRDBAND 0x080
#define EPOLLWRNORM 0x100
#define EPOLLWRBAND 0x200
#define EPOLLMSG 0x400

#define EPOLLRDHUP 0x2000

#define EPOLLEXCLUSIVE 0x10000000
#define EPOLLONESHOT 0x40000000
#define EPOLLET 0x80000000

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data
{
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event
{
    uint32_t events;
    epoll_data_t data;
};

int epoll_create(int size);

int epoll_create(int size)
{
    return -1;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return -1;
}

int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout);

int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout)
{
    return -1;
}

#if (NGX_HAVE_EVENTFD)
#define SYS_eventfd 323
#endif

#if (NGX_HAVE_FILE_AIO)

#define SYS_io_setup 245
#define SYS_io_destroy 246
#define SYS_io_getevents 247

typedef u_int aio_context_t;

struct io_event
{
    uint64_t data; /* the data field from the iocb */
    uint64_t obj;  /* what iocb this event came from */
    int64_t res;   /* result code for this event */
    int64_t res2;  /* secondary result */
};

#endif
#endif /* NGX_TEST_BUILD_EPOLL */

/*
 * 结构体 ngx_epoll_conf_t 用于表示 epoll 模块的配置。
 * 它包含两个 ngx_uint_t 类型的变量：'events' 和 'aio_requests'。
 * 'events' 存储 epoll 能够处理的最大事件数。
 * 'aio_requests' 存储异步 I/O 请求的最大数量。
 */
typedef struct
{
    ngx_uint_t events;
    ngx_uint_t aio_requests;
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify_init(ngx_log_t *log);
static void ngx_epoll_notify_handler(ngx_event_t *ev);
#endif
#if (NGX_HAVE_EPOLLRDHUP)
static void ngx_epoll_test_rdhup(ngx_cycle_t *cycle);
#endif
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
                                     ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
                                     ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
                                          ngx_uint_t flags);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify(ngx_event_handler_pt handler);
#endif
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
                                          ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int ep = -1;
static struct epoll_event *event_list;
static ngx_uint_t nevents;

#if (NGX_HAVE_EVENTFD)
static int notify_fd = -1;
static ngx_event_t notify_event;
static ngx_connection_t notify_conn;
#endif

#if (NGX_HAVE_FILE_AIO)

int ngx_eventfd = -1;
aio_context_t ngx_aio_ctx = 0;

static ngx_event_t ngx_eventfd_event;
static ngx_connection_t ngx_eventfd_conn;

#endif

#if (NGX_HAVE_EPOLLRDHUP)
ngx_uint_t ngx_use_epoll_rdhup;
#endif

/*
 * 描述：这个结构体保存了epoll模块的配置指令。
 *       它包括与epoll事件和工作进程异步I/O请求相关的设置。
 */
static ngx_str_t epoll_name = ngx_string("epoll");

/*
 * 描述：epoll模块的配置指令。
 *       这些指令定义了与epoll相关的配置参数。
 */
static ngx_command_t ngx_epoll_commands[] = {

    /*
     * 描述：设置在单个epoll_wait调用中可以处理的最大事件数。
     *       此指令接受一个参数，即事件数。
     */
    {ngx_string("epoll_events"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot,
     0,
     offsetof(ngx_epoll_conf_t, events),
     NULL},

    /*
     * 描述：设置工作进程的异步I/O请求数量。
     *       此指令接受一个参数，即请求数量。
     */
    {ngx_string("worker_aio_requests"),
     NGX_EVENT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot,
     0,
     offsetof(ngx_epoll_conf_t, aio_requests),
     NULL},

    ngx_null_command};


/*
 * 描述：定义epoll事件模块的上下文结构体，包含事件模块的各种回调函数。
 */
static ngx_event_module_t ngx_epoll_module_ctx = {
    &epoll_name,                  /* 模块名 */
    ngx_epoll_create_conf,        /* 创建配置 */
    ngx_epoll_init_conf,          /* 初始化配置 */

    {
        ngx_epoll_add_event,      /* 添加事件 */
        ngx_epoll_del_event,      /* 删除事件 */
        ngx_epoll_add_event,      /* 启用事件 */
        ngx_epoll_del_event,      /* 禁用事件 */
        ngx_epoll_add_connection, /* 添加连接 */
        ngx_epoll_del_connection, /* 删除连接 */
#if (NGX_HAVE_EVENTFD)
        ngx_epoll_notify,         /* 触发通知 */
#else
        NULL,                     /* 触发通知 */
#endif
        ngx_epoll_process_events, /* 处理事件 */
        ngx_epoll_init,           /* 初始化事件 */
        ngx_epoll_done,           /* 完成事件处理 */
    }
};


ngx_module_t ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx, /* module context */
    ngx_epoll_commands,    /* module directives */
    NGX_EVENT_MODULE,      /* module type */
    NULL,                  /* init master */
    NULL,                  /* init module */
    NULL,                  /* init process */
    NULL,                  /* init thread */
    NULL,                  /* exit thread */
    NULL,                  /* exit process */
    NULL,                  /* exit master */
    NGX_MODULE_V1_PADDING};

#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 */

static int
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}

static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}

static int
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
             struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}

static void
ngx_epoll_aio_init(ngx_cycle_t *cycle, ngx_epoll_conf_t *epcf)
{
    int n;
    struct epoll_event ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    ngx_eventfd = eventfd(0, 0);
#else
    ngx_eventfd = syscall(SYS_eventfd, 0);
#endif

    if (ngx_eventfd == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "eventfd() failed");
        ngx_file_aio = 0;
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "eventfd: %d", ngx_eventfd);

    n = 1;

    if (ioctl(ngx_eventfd, FIONBIO, &n) == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "ioctl(eventfd, FIONBIO) failed");
        goto failed;
    }

    if (io_setup(epcf->aio_requests, &ngx_aio_ctx) == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "io_setup() failed");
        goto failed;
    }

    ngx_eventfd_event.data = &ngx_eventfd_conn;
    ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;
    ngx_eventfd_event.log = cycle->log;
    ngx_eventfd_event.active = 1;
    ngx_eventfd_conn.fd = ngx_eventfd;
    ngx_eventfd_conn.read = &ngx_eventfd_event;
    ngx_eventfd_conn.log = cycle->log;

    ee.events = EPOLLIN | EPOLLET;
    ee.data.ptr = &ngx_eventfd_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) != -1)
    {
        return;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

    if (io_destroy(ngx_aio_ctx) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "io_destroy() failed");
    }

failed:

    if (close(ngx_eventfd) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    ngx_eventfd = -1;
    ngx_aio_ctx = 0;
    ngx_file_aio = 0;
}

#endif

/*
 * 初始化 epoll 事件模块。
 * 参数 cycle 为当前事件循环的上下文，timer 为定时器的时间。
 */
static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    // 获取 epoll 模块的配置信息
    ngx_epoll_conf_t *epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);

    // 如果全局变量 ep 为 -1，说明 epoll 还未初始化，进行初始化操作
    if (ep == -1)
    {
        // 创建 epoll 实例，参数为连接数的一半
        ep = epoll_create(cycle->connection_n / 2);

        // 创建失败时记录错误日志并返回 NGX_ERROR
        if (ep == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() 失败");
            return NGX_ERROR;
        }

        // 初始化通知机制，如果支持 eventfd，则调用 ngx_epoll_notify_init 进行初始化
#if (NGX_HAVE_EVENTFD)
        if (ngx_epoll_notify_init(cycle->log) != NGX_OK)
        {
            ngx_epoll_module_ctx.actions.notify = NULL;
        }
#endif

        // 初始化文件异步 I/O，如果支持文件异步 I/O
#if (NGX_HAVE_FILE_AIO)
        ngx_epoll_aio_init(cycle, epcf);
#endif

        // 测试是否支持 EPOLLRDHUP
#if (NGX_HAVE_EPOLLRDHUP)
        ngx_epoll_test_rdhup(cycle);
#endif
    }

    // 如果当前事件数组大小小于配置的事件数，则重新分配内存
    if (nevents < epcf->events)
    {
        // 释放之前分配的内存
        if (event_list)
        {
            ngx_free(event_list);
        }

        // 根据新的事件数重新分配内存
        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        // 分配内存失败时返回 NGX_ERROR
        if (event_list == NULL)
        {
            return NGX_ERROR;
        }
    }

    // 更新当前事件数组大小为配置的事件数
    nevents = epcf->events;

    // 设置全局变量 ngx_io 为系统的 I/O 操作
    ngx_io = ngx_os_io;

    // 设置全局事件操作函数为 epoll 模块的操作函数
    ngx_event_actions = ngx_epoll_module_ctx.actions;

    // 设置全局事件标志，使用 ET 模式和贪婪模式
#if (NGX_HAVE_CLEAR_EVENT)
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      | NGX_USE_GREEDY_EVENT | NGX_USE_EPOLL_EVENT;

    // 返回初始化成功
    return NGX_OK;
}

{
    ngx_epoll_conf_t *epcf;

    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);

    if (ep == -1)
    {
        ep = epoll_create(cycle->connection_n / 2);

        if (ep == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() failed");
            return NGX_ERROR;
        }

#if (NGX_HAVE_EVENTFD)
        if (ngx_epoll_notify_init(cycle->log) != NGX_OK)
        {
            ngx_epoll_module_ctx.actions.notify = NULL;
        }
#endif

#if (NGX_HAVE_FILE_AIO)
        ngx_epoll_aio_init(cycle, epcf);
#endif

#if (NGX_HAVE_EPOLLRDHUP)
        ngx_epoll_test_rdhup(cycle);
#endif
    }

    if (nevents < epcf->events)
    {
        if (event_list)
        {
            ngx_free(event_list);
        }

        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        if (event_list == NULL)
        {
            return NGX_ERROR;
        }
    }

    nevents = epcf->events;

    ngx_io = ngx_os_io;

    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT)
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      | NGX_USE_GREEDY_EVENT | NGX_USE_EPOLL_EVENT;

    return NGX_OK;
}

#if (NGX_HAVE_EVENTFD)

static ngx_int_t
ngx_epoll_notify_init(ngx_log_t *log)
{
    struct epoll_event ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    notify_fd = eventfd(0, 0);
#else
    notify_fd = syscall(SYS_eventfd, 0);
#endif

    if (notify_fd == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "eventfd() failed");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                   "notify eventfd: %d", notify_fd);

    notify_event.handler = ngx_epoll_notify_handler;
    notify_event.log = log;
    notify_event.active = 1;

    notify_conn.fd = notify_fd;
    notify_conn.read = &notify_event;
    notify_conn.log = log;

    ee.events = EPOLLIN | EPOLLET;
    ee.data.ptr = &notify_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, notify_fd, &ee) == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

        if (close(notify_fd) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                          "eventfd close() failed");
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}

static void
ngx_epoll_notify_handler(ngx_event_t *ev)
{
    ssize_t n;
    uint64_t count;
    ngx_err_t err;
    ngx_event_handler_pt handler;

    if (++ev->index == NGX_MAX_UINT32_VALUE)
    {
        ev->index = 0;

        n = read(notify_fd, &count, sizeof(uint64_t));

        err = ngx_errno;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "read() eventfd %d: %z count:%uL", notify_fd, n, count);

        if ((size_t)n != sizeof(uint64_t))
        {
            ngx_log_error(NGX_LOG_ALERT, ev->log, err,
                          "read() eventfd %d failed", notify_fd);
        }
    }

    handler = ev->data;
    handler(ev);
}

#endif

#if (NGX_HAVE_EPOLLRDHUP)

static void
ngx_epoll_test_rdhup(ngx_cycle_t *cycle)
{
    int s[2], events;
    struct epoll_event ee;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "socketpair() failed");
        return;
    }

    ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, s[0], &ee) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_ctl() failed");
        goto failed;
    }

    if (close(s[1]) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
        s[1] = -1;
        goto failed;
    }

    s[1] = -1;

    events = epoll_wait(ep, &ee, 1, 5000);

    if (events == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_wait() failed");
        goto failed;
    }

    if (events)
    {
        ngx_use_epoll_rdhup = ee.events & EPOLLRDHUP;
    }
    else
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, NGX_ETIMEDOUT,
                      "epoll_wait() timed out");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "testing the EPOLLRDHUP flag: %s",
                   ngx_use_epoll_rdhup ? "success" : "fail");

failed:

    if (s[1] != -1 && close(s[1]) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }

    if (close(s[0]) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }
}

#endif

static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
    if (close(ep) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll close() failed");
    }

    ep = -1;

#if (NGX_HAVE_EVENTFD)

    if (close(notify_fd) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    notify_fd = -1;

#endif

#if (NGX_HAVE_FILE_AIO)

    if (ngx_eventfd != -1)
    {

        if (io_destroy(ngx_aio_ctx) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "io_destroy() failed");
        }

        if (close(ngx_eventfd) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "eventfd close() failed");
        }

        ngx_eventfd = -1;
    }

    ngx_aio_ctx = 0;

#endif

    ngx_free(event_list);

    event_list = NULL;
    nevents = 0;
}

/*
 * 向 epoll 中添加事件的函数。
 * 该函数将被 ngx_epoll_module 模块的 add_event 回调函数调用。
 * 参数 ev: 指向 ngx_event_t 结构体的指针，表示要添加的事件。
 * 参数 event: 表示要添加的事件类型，可以是 NGX_READ_EVENT 或 NGX_WRITE_EVENT。
 * 参数 flags: 表示事件的标志，例如 NGX_ONESHOT_EVENT 和 NGX_EXCLUSIVE_EVENT。
 * 返回值: NGX_OK 表示成功添加事件，NGX_ERROR 表示添加失败。
 */
static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int op;
    uint32_t events, prev;
    ngx_event_t *e;
    ngx_connection_t *c;
    struct epoll_event ee;

    // 获取事件对应的连接结构体
    c = ev->data;

    // 根据事件类型设置 epoll 事件的标志
    events = (uint32_t)event;

    if (event == NGX_READ_EVENT)
    {
        e = c->write;
        prev = EPOLLOUT;
#if (NGX_READ_EVENT != EPOLLIN | EPOLLRDHUP)
        events = EPOLLIN | EPOLLRDHUP;
#endif
    }
    else
    {
        e = c->read;
        prev = EPOLLIN | EPOLLRDHUP;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }

    // 如果事件已经是活跃状态，则使用 EPOLL_CTL_MOD 修改事件
    if (e->active)
    {
        op = EPOLL_CTL_MOD;
        events |= prev;
    }
    else
    {
        op = EPOLL_CTL_ADD;  // 否则，使用 EPOLL_CTL_ADD 添加事件
    }

    // 根据标志设置 epoll 事件的属性
#if (NGX_HAVE_EPOLLEXCLUSIVE && NGX_HAVE_EPOLLRDHUP)
    if (flags & NGX_EXCLUSIVE_EVENT)
    {
        events &= ~EPOLLRDHUP;
    }
#endif

    // 设置 epoll_event 结构体的属性
    ee.events = events | (uint32_t)flags;
    ee.data.ptr = (void *)((uintptr_t)c | ev->instance);

    // 在调试日志中记录 epoll 操作信息
    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll add event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    // 调用 epoll_ctl 函数进行事件控制操作
    if (epoll_ctl(ep, op, c->fd, &ee) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;  // 操作失败，返回 NGX_ERROR
    }

    ev->active = 1;  // 设置事件为活跃状态
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif

    return NGX_OK;  // 添加事件成功，返回 NGX_OK
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int op;
    uint32_t prev;
    ngx_event_t *e;
    ngx_connection_t *c;
    struct epoll_event ee;

    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT)
    {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT)
    {
        e = c->write;
        prev = EPOLLOUT;
    }
    else
    {
        e = c->read;
        prev = EPOLLIN | EPOLLRDHUP;
    }

    if (e->active)
    {
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t)flags;
        ee.data.ptr = (void *)((uintptr_t)c | ev->instance);
    }
    else
    {
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    ev->active = 0;

    return NGX_OK;
}

static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event ee;

    ee.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ee.data.ptr = (void *)((uintptr_t)c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);

    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }

    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int op;
    struct epoll_event ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT)
    {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}

#if (NGX_HAVE_EVENTFD)

static ngx_int_t
ngx_epoll_notify(ngx_event_handler_pt handler)
{
    static uint64_t inc = 1;

    notify_event.data = handler;

    if ((size_t)write(notify_fd, &inc, sizeof(uint64_t)) != sizeof(uint64_t))
    {
        ngx_log_error(NGX_LOG_ALERT, notify_event.log, ngx_errno,
                      "write() to eventfd %d failed", notify_fd);
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif

/*
 * 处理 epoll 模型的事件。
 *
 * 参数:
 *   cycle: 指向 ngx_cycle_t 结构的指针，表示当前的循环。
 *   timer: 等待事件的超时时间，单位是毫秒。
 *   flags: 控制处理的标志，如更新时间、处理定时器事件等。
 *
 * 返回值:
 *   若处理成功，返回 NGX_OK；否则返回 NGX_ERROR。
 */
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    int events;
    uint32_t revents;
    ngx_int_t instance, i;
    ngx_uint_t level;
    ngx_err_t err;
    ngx_event_t *rev, *wev;
    ngx_queue_t *queue;
    ngx_connection_t *c;

    // 使用 epoll_wait 等待事件的发生
    events = epoll_wait(ep, event_list, (int)nevents, timer);

    // 获取 epoll_wait 的返回值，判断是否出错
    err = (events == -1) ? ngx_errno : 0;

    // 如果需要更新时间或者有定时器事件发生，更新时间
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm)
    {
        ngx_time_update();
    }

    // 处理 epoll_wait 的返回值
    if (err)
    {
        // 若是被中断，则检查是否是定时器超时触发，是的话返回成功
        if (err == NGX_EINTR)
        {
            if (ngx_event_timer_alarm)
            {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }
            level = NGX_LOG_INFO;
        }
        else
        {
            level = NGX_LOG_ALERT;
        }

        // 记录错误日志并返回失败
        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    // 若没有事件发生，且不是永久等待，返回成功
    if (events == 0)
    {
        if (timer != NGX_TIMER_INFINITE)
        {
            return NGX_OK;
        }

        // 如果永久等待且没有事件发生，则记录错误日志并返回失败
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }

    // 遍历处理所有发生的事件
    for (i = 0; i < events; i++)
    {
        c = event_list[i].data.ptr;

        // 获取事件实例信息
        instance = (uintptr_t)c & 1;
        c = (ngx_connection_t *)((uintptr_t)c & (uintptr_t)~1);

        // 获取读事件
        rev = c->read;

        // 检查事件的合法性
        if (c->fd == -1 || rev->instance != instance)
        {
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

        // 获取事件发生的具体类型
        revents = event_list[i].events;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        // 处理读事件
        if (revents & (EPOLLERR | EPOLLHUP))
        {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);

            // 处理错误事件，添加 EPOLLIN 和 EPOLLOUT 以处理至少一个激活的处理程序
            revents |= EPOLLIN | EPOLLOUT;
        }

        // 处理读事件
        if ((revents & EPOLLIN) && rev->active)
        {
            // 处理 EPOLLRDHUP 事件
#if (NGX_HAVE_EPOLLRDHUP)
            if (revents & EPOLLRDHUP)
            {
                rev->pending_eof = 1;
            }
#endif

            // 设置读事件为就绪状态，等待处理
            rev->ready = 1;
            rev->available = -1;

            // 根据标志处理事件，加入到对应的事件队列或者直接调用事件处理函数
            if (flags & NGX_POST_EVENTS)
            {
                queue = rev->accept ? &ngx_posted_accept_events
                                    : &ngx_posted_events;
                ngx_post_event(rev, queue);
            }
            else
            {
                rev->handler(rev);
            }
        }

        // 处理写事件
        wev = c->write;
        if ((revents & EPOLLOUT) && wev->active)
        {
            // 检查事件的合法性
            if (c->fd == -1 || wev->instance != instance)
            {
                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            // 设置写事件为就绪状态，等待处理
            wev->ready = 1;
#if (NGX_THREADS)
            wev->complete = 1;
#endif

            // 根据标志处理事件，加入到对应的事件队列或者直接调用事件处理函数
            if (flags & NGX_POST_EVENTS)
            {
                ngx_post_event(wev, &ngx_posted_events);
            }
            else
            {
                wev->handler(wev);
            }
        }
    }

    return NGX_OK;
}

{
    int events;
    uint32_t revents;
    ngx_int_t instance, i;
    ngx_uint_t level;
    ngx_err_t err;
    ngx_event_t *rev, *wev;
    ngx_queue_t *queue;
    ngx_connection_t *c;

    /* NGX_TIMER_INFINITE == INFTIM */

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "epoll timer: %M", timer);

    events = epoll_wait(ep, event_list, (int)nevents, timer);

    err = (events == -1) ? ngx_errno : 0;

    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm)
    {
        ngx_time_update();
    }

    if (err)
    {
        if (err == NGX_EINTR)
        {

            if (ngx_event_timer_alarm)
            {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            level = NGX_LOG_INFO;
        }
        else
        {
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    if (events == 0)
    {
        if (timer != NGX_TIMER_INFINITE)
        {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }

    for (i = 0; i < events; i++)
    {
        c = event_list[i].data.ptr;

        instance = (uintptr_t)c & 1;
        c = (ngx_connection_t *)((uintptr_t)c & (uintptr_t)~1);

        rev = c->read;

        if (c->fd == -1 || rev->instance != instance)
        {

            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

        revents = event_list[i].events;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        if (revents & (EPOLLERR | EPOLLHUP))
        {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);

            /*
             * if the error events were returned, add EPOLLIN and EPOLLOUT
             * to handle the events at least in one active handler
             */

            revents |= EPOLLIN | EPOLLOUT;
        }

#if 0
        if (revents & ~(EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange epoll_wait() events fd:%d ev:%04XD",
                          c->fd, revents);
        }
#endif

        if ((revents & EPOLLIN) && rev->active)
        {

#if (NGX_HAVE_EPOLLRDHUP)
            if (revents & EPOLLRDHUP)
            {
                rev->pending_eof = 1;
            }
#endif

            rev->ready = 1;
            rev->available = -1;

            if (flags & NGX_POST_EVENTS)
            {
                queue = rev->accept ? &ngx_posted_accept_events
                                    : &ngx_posted_events;

                ngx_post_event(rev, queue);
            }
            else
            {
                rev->handler(rev);
            }
        }

        wev = c->write;

        if ((revents & EPOLLOUT) && wev->active)
        {

            if (c->fd == -1 || wev->instance != instance)
            {

                /*
                 * the stale event from a file descriptor
                 * that was just closed in this iteration
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            wev->ready = 1;
#if (NGX_THREADS)
            wev->complete = 1;
#endif

            if (flags & NGX_POST_EVENTS)
            {
                ngx_post_event(wev, &ngx_posted_events);
            }
            else
            {
                wev->handler(wev);
            }
        }
    }

    return NGX_OK;
}

#if (NGX_HAVE_FILE_AIO)

static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int n, events;
    long i;
    uint64_t ready;
    ngx_err_t err;
    ngx_event_t *e;
    ngx_event_aio_t *aio;
    struct io_event event[64];
    struct timespec ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");

    n = read(ngx_eventfd, &ready, 8);

    err = ngx_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);

    if (n != 8)
    {
        if (n == -1)
        {
            if (err == NGX_EAGAIN)
            {
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }

        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "read(eventfd) returned only %d bytes", n);
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready)
    {

        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "io_getevents: %d", events);

        if (events > 0)
        {
            ready -= events;

            for (i = 0; i < events; i++)
            {

                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %XL %XL %L %L",
                               event[i].data, event[i].obj,
                               event[i].res, event[i].res2);

                e = (ngx_event_t *)(uintptr_t)event[i].data;

                e->complete = 1;
                e->active = 0;
                e->ready = 1;

                aio = e->data;
                aio->res = event[i].res;

                ngx_post_event(e, &ngx_posted_events);
            }

            continue;
        }

        if (events == 0)
        {
            return;
        }

        /* events == -1 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "io_getevents() failed");
        return;
    }
}

#endif

/*
 * 创建并初始化 ngx_epoll 模块的配置结构体。
 * 该函数将被 ngx_epoll_module 模块的 create_conf 回调函数调用。
 * 参数 cycle: 指向 ngx_cycle_t 结构体的指针，表示当前的运行周期。
 * 返回值: 指向 ngx_epoll_conf_t 结构体的指针，表示创建的配置结构体。
 */
static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{
    ngx_epoll_conf_t *epcf;

    // 分配内存空间用于存储 ngx_epoll_conf_t 结构体
    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL)
    {
        return NULL;  // 内存分配失败，返回 NULL
    }

    // 初始化配置结构体的成员变量
    epcf->events = NGX_CONF_UNSET;        // events 默认值为 NGX_CONF_UNSET
    epcf->aio_requests = NGX_CONF_UNSET;  // aio_requests 默认值为 NGX_CONF_UNSET

    return epcf;  // 返回创建并初始化后的配置结构体指针
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->events, 512);
    ngx_conf_init_uint_value(epcf->aio_requests, 32);

    return NGX_CONF_OK;
}
