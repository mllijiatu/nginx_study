
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    ngx_socket_t        fd;

    struct sockaddr    *sockaddr;
    socklen_t           socklen;    /* size of sockaddr */
    size_t              addr_text_max_len;
    ngx_str_t           addr_text;

    int                 type;

    int                 backlog;
    int                 rcvbuf;
    int                 sndbuf;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                 keepidle;
    int                 keepintvl;
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;

    void               *servers;  /* array of ngx_http_in_addr_t, for example */

    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;

    ngx_listening_t    *previous;
    ngx_connection_t   *connection;

    ngx_rbtree_t        rbtree;
    ngx_rbtree_node_t   sentinel;

    ngx_uint_t          worker;

    unsigned            open:1;
    unsigned            remain:1;
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    unsigned            listen:1;
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1;
    unsigned            wildcard:1;

#if (NGX_HAVE_INET6)
    unsigned            ipv6only:1;
#endif
    unsigned            reuseport:1;
    unsigned            add_reuseport:1;
    unsigned            keepalive:2;

    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    char               *accept_filter;
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    int                 fastopen;
#endif

};


typedef enum {
    NGX_ERROR_ALERT = 0,
    NGX_ERROR_ERR,
    NGX_ERROR_INFO,
    NGX_ERROR_IGNORE_ECONNRESET,
    NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
    NGX_TCP_NODELAY_UNSET = 0,
    NGX_TCP_NODELAY_SET,
    NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
    NGX_TCP_NOPUSH_UNSET = 0,
    NGX_TCP_NOPUSH_SET,
    NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01
#define NGX_HTTP_V2_BUFFERED   0x02


/*
 * 结构体 ngx_connection_s 表示一个连接对象，用于描述一个客户端与服务器的连接。
 */
struct ngx_connection_s {
    void               *data;                 // 保留指针，可以由用户自定义使用
    ngx_event_t        *read;                 // 读事件对象
    ngx_event_t        *write;                // 写事件对象

    ngx_socket_t        fd;                   // 连接的套接字描述符

    ngx_recv_pt         recv;                 // 接收数据的回调函数指针
    ngx_send_pt         send;                 // 发送数据的回调函数指针
    ngx_recv_chain_pt   recv_chain;           // 链式接收数据的回调函数指针
    ngx_send_chain_pt   send_chain;           // 链式发送数据的回调函数指针

    ngx_listening_t    *listening;            // 监听对象，指向当前连接所属的监听对象

    off_t               sent;                 // 表示已经发送出去的字节数

    ngx_log_t          *log;                  // 连接相关的日志对象

    ngx_pool_t         *pool;                 // 内存池对象，用于分配内存

    int                 type;                 // 连接的类型

    struct sockaddr    *sockaddr;             // 远端地址
    socklen_t           socklen;              // 远端地址长度
    ngx_str_t           addr_text;            // 字符串形式的远端地址

    ngx_proxy_protocol_t  *proxy_protocol;    // 代理协议相关信息

#if (NGX_SSL || NGX_COMPAT)
    ngx_ssl_connection_t  *ssl;               // SSL/TLS 连接相关信息
#endif

    ngx_udp_connection_t  *udp;               // UDP 连接相关信息

    struct sockaddr    *local_sockaddr;      // 本地地址
    socklen_t           local_socklen;        // 本地地址长度

    ngx_buf_t          *buffer;               // 用于接收和发送数据的缓冲区

    ngx_queue_t         queue;                // 用于将连接对象加入到某个队列中

    ngx_atomic_uint_t   number;               // 连接的编号，用于标识连接

    ngx_msec_t          start_time;           // 连接建立的时间
    ngx_uint_t          requests;             // 处理的请求数量

    unsigned            buffered:8;          // 缓冲区中的数据长度

    unsigned            log_error:3;         // 记录错误日志的级别，ngx_connection_log_error_e 类型

    unsigned            timedout:1;          // 连接是否超时
    unsigned            error:1;             // 连接是否发生错误
    unsigned            destroyed:1;         // 连接是否已销毁
    unsigned            pipeline:1;          // 是否使用 HTTP 管线方式

    unsigned            idle:1;              // 连接是否空闲
    unsigned            reusable:1;          // 连接是否可重用
    unsigned            close:1;             // 连接是否关闭
    unsigned            shared:1;            // 连接是否共享

    unsigned            sendfile:1;          // 是否使用 sendfile
    unsigned            sndlowat:1;          // 是否使用 lowat 设置 TCP 发送缓冲区下限
    unsigned            tcp_nodelay:2;       // TCP_NODELAY 状态，ngx_connection_tcp_nodelay_e 类型
    unsigned            tcp_nopush:2;        // TCP_NOPUSH 状态，ngx_connection_tcp_nopush_e 类型

    unsigned            need_last_buf:1;     // 是否需要最后一块缓冲区
    unsigned            need_flush_buf:1;    // 是否需要刷新缓冲区

#if (NGX_HAVE_SENDFILE_NODISKIO || NGX_COMPAT)
    unsigned            busy_count:2;        // 连接繁忙状态计数
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_thread_task_t  *sendfile_task;        // 用于多线程发送文件任务
#endif
};



#define ngx_set_connection_log(c, l)                                         \
                                                                             \
    c->log->file = l->file;                                                  \
    c->log->next = l->next;                                                  \
    c->log->writer = l->writer;                                              \
    c->log->wdata = l->wdata;                                                \
    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {                   \
        c->log->log_level = l->log_level;                                    \
    }


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, struct sockaddr *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_clone_listening(ngx_cycle_t *cycle, ngx_listening_t *ls);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
void ngx_close_idle_connections(ngx_cycle_t *cycle);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_tcp_nodelay(ngx_connection_t *c);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */
