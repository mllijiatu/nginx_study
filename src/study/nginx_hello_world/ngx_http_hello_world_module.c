#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <inaddr.h>

/*
 * 该函数用于配置指令"hello_world"，指定处理函数为ngx_http_hello_world。
 */
static char *ngx_http_hello_world(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/*
 * 定义ngx_http_hello_world模块的指令数组，包含"hello_world"指令。
 * NGX_HTTP_MAIN_CONF——指令出现在全局配置部分是合法的；NGX_HTTP_SRV_CONF——指令出现在server主机配置部分是合法的；
 * NGX_HTTP_LOC_CONF——指令出现在location配置部分是合法的；NGX_HTTP_UPS_CONF——指令出现在upstream配置部分是合法的；
 * NGX_CONF_NOARGS——指令没有参数；NGX_CONF_TAKE1——指令读入1个参数；NGX_CONF_TAKE2——指令读入2个参数；
 * NGX_CONF_TAKE7——指令读入7个参数；NGX_CONF_FLAG——指令读入1个布尔型数据；NGX_CONF_1MORE——指令至少读入1个参数；NGX_CONF_2MORE——指令至少读入2个参数。
 */
static ngx_command_t ngx_http_hello_world_commands[] = {
    {
        ngx_string("hello_world"),           // 指令名称
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS, // 指令类型，只能出现在location配置块中，无参数
        ngx_http_hello_world,                // 指令处理函数
        0,                                   // 配置项在结构体中的偏移量
        0,                                   // 配置项在结构体中的特殊标志位
        NULL                                 // 配置项附加数据
    },
    ngx_null_command // 结束指令数组的标志
};

/*
 * 定义"hello world"字符串
 */
static u_char ngx_hello_world[] = "hello world";

/*
 * 定义ngx_http_hello_world模块的上下文结构体，包含各个阶段的处理函数。
 * preconfiguration——在读入配置前调用；postconfiguration——在读入配置后调用；
 * create main configuration——在创建全局部分配置时调用（比如，用来分配空间和设置默认值）；
 * init main configuration——在初始化全局部分的配置时调用（比如，把原来的默认值用nginx.conf读到的值覆盖）；
 * create server configuration——在创建虚拟主机部分的配置时调用；merge server configuration——与全局部分配置合并时调用；
 * create location configuration——创建位置部分的配置时调用；merge location configuration——与主机部分配置合并时调用。
 */
static ngx_http_module_t ngx_http_hello_world_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */
    NULL, /* create main configuration */
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* merge server configuration */
    NULL, /* create location configuration */
    NULL  /* merge location configuration */
};

/*
 * 定义ngx_http_hello_world模块，包括版本信息、上下文结构体、指令数组等信息。
 */
ngx_module_t ngx_http_hello_world_module = {
    NGX_MODULE_V1,                    // 模块版本信息
    &ngx_http_hello_world_module_ctx, // 模块上下文结构体
    ngx_http_hello_world_commands,    // 模块指令数组
    NGX_HTTP_MODULE,                  // 模块类型，这是一个HTTP模块
    NULL,                             // 在master进程初始化时调用的函数
    NULL,                             // 在模块初始化时调用的函数
    NULL,                             // 在worker进程初始化时调用的函数
    NULL,                             // 在工作线程初始化时调用的函数
    NULL,                             // 在工作线程退出时调用的函数
    NULL,                             // 在worker进程退出时调用的函数
    NULL,                             // 在master进程退出时调用的函数
    NGX_MODULE_V1_PADDING             // 模块保留字段，必须为NGX_MODULE_V1_PADDING
};

/*
 * 该函数用于处理HTTP请求，向客户端返回"hello world"字符串。
 * 参数ngx_http_request_t *r，可以让我们访问到客户端的头部和不久要发送的回复头部，包含两个成员：r->headers_in和r->headers_out。
 */
static ngx_int_t ngx_http_hello_world_handler(ngx_http_request_t *r)
{
    ngx_buf_t *b;
    ngx_chain_t out;

    // 设置响应的Content-Type为"text/plain"
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *)"text/plain";

    // 为响应分配内存
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    out.buf = b;
    out.next = NULL;

    // 设置缓冲区的数据为"hello world"
    b->pos = ngx_hello_world;
    b->last = ngx_hello_world + sizeof(ngx_hello_world);
    b->memory = 1;
    b->last_buf = 1;

    // 设置HTTP响应状态为OK
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(ngx_hello_world);

    // 发送HTTP响应头
    ngx_http_send_header(r);

    // 输出响应内容
    return ngx_http_output_filter(r, &out);
}

/*
 * 该函数用于配置指令"hello_world"，指定处理函数为ngx_http_hello_world_handler。
 */
static char *ngx_http_hello_world(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf;

    // 获取HTTP核心模块的配置
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    // 设置处理函数为ngx_http_hello_world_handler
    clcf->handler = ngx_http_hello_world_handler;

    return NGX_CONF_OK;
}
