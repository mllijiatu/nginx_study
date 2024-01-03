
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

static void ngx_destroy_cycle_pools(ngx_conf_t *conf);
static ngx_int_t ngx_init_zone_pool(ngx_cycle_t *cycle,
                                    ngx_shm_zone_t *shm_zone);
static ngx_int_t ngx_test_lockfile(u_char *file, ngx_log_t *log);
static void ngx_clean_old_cycles(ngx_event_t *ev);
static void ngx_shutdown_timer_handler(ngx_event_t *ev);

volatile ngx_cycle_t *ngx_cycle;
ngx_array_t ngx_old_cycles;

static ngx_pool_t *ngx_temp_pool;
static ngx_event_t ngx_cleaner_event;
static ngx_event_t ngx_shutdown_event;

ngx_uint_t ngx_test_config;
ngx_uint_t ngx_dump_config;
ngx_uint_t ngx_quiet_mode;

/* STUB NAME */
static ngx_connection_t dumb;
/* STUB */

/**
 * @brief 初始化NGINX的全局cycle结构体
 *
 * @param old_cycle 旧的cycle结构体，用于平滑升级
 * @return 返回新的cycle结构体
 */
ngx_cycle_t *
ngx_init_cycle(ngx_cycle_t *old_cycle)
{
    void *rv;
    char **senv;
    ngx_uint_t i, n;
    ngx_log_t *log;
    ngx_time_t *tp;
    ngx_conf_t conf;
    ngx_pool_t *pool;
    ngx_cycle_t *cycle, **old;
    ngx_shm_zone_t *shm_zone, *oshm_zone;
    ngx_list_part_t *part, *opart;
    ngx_open_file_t *file;
    ngx_listening_t *ls, *nls;
    ngx_core_conf_t *ccf, *old_ccf;
    ngx_core_module_t *module;
    char hostname[NGX_MAXHOSTNAMELEN];

    ngx_timezone_update();

    /* 强制使用新的时区更新本地时间 */
    tp = ngx_timeofday();
    tp->sec = 0;
    ngx_time_update();

    log = old_cycle->log;

    // 创建新的内存池
    pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (pool == NULL)
    {
        return NULL;
    }
    pool->log = log;

    // 分配新的cycle结构体
    cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t));
    if (cycle == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    cycle->pool = pool;
    cycle->log = log;
    cycle->old_cycle = old_cycle;

    // 设置配置文件路径前缀
    cycle->conf_prefix.len = old_cycle->conf_prefix.len;
    cycle->conf_prefix.data = ngx_pstrdup(pool, &old_cycle->conf_prefix);
    if (cycle->conf_prefix.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // Nginx的路径前缀拷贝
    cycle->prefix.len = old_cycle->prefix.len;
    cycle->prefix.data = ngx_pstrdup(pool, &old_cycle->prefix);
    if (cycle->prefix.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    cycle->error_log.len = old_cycle->error_log.len;
    cycle->error_log.data = ngx_pnalloc(pool, old_cycle->error_log.len + 1);
    if (cycle->error_log.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_cpystrn(cycle->error_log.data, old_cycle->error_log.data,
                old_cycle->error_log.len + 1);

    // 拷贝配置文件信息
    cycle->conf_file.len = old_cycle->conf_file.len;
    cycle->conf_file.data = ngx_pnalloc(pool, old_cycle->conf_file.len + 1);
    if (cycle->conf_file.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_cpystrn(cycle->conf_file.data, old_cycle->conf_file.data,
                old_cycle->conf_file.len + 1);

    // 拷贝配置参数信息
    cycle->conf_param.len = old_cycle->conf_param.len;
    cycle->conf_param.data = ngx_pstrdup(pool, &old_cycle->conf_param);
    if (cycle->conf_param.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 初始化路径数组
    n = old_cycle->paths.nelts ? old_cycle->paths.nelts : 10;

    if (ngx_array_init(&cycle->paths, pool, n, sizeof(ngx_path_t *)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 初始化配置信息转储数组
    if (ngx_array_init(&cycle->config_dump, pool, 1, sizeof(ngx_conf_dump_t)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_rbtree_init(&cycle->config_dump_rbtree, &cycle->config_dump_sentinel,
                    ngx_str_rbtree_insert_value);

    // 计算打开文件的数量
    if (old_cycle->open_files.part.nelts)
    {
        n = old_cycle->open_files.part.nelts;
        for (part = old_cycle->open_files.part.next; part; part = part->next)
        {
            n += part->nelts;
        }
    }
    else
    {
        n = 20;
    }

    // 初始化打开文件的链表
    if (ngx_list_init(&cycle->open_files, pool, n, sizeof(ngx_open_file_t)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 计算共享内存的数量
    if (old_cycle->shared_memory.part.nelts)
    {
        n = old_cycle->shared_memory.part.nelts;
        for (part = old_cycle->shared_memory.part.next; part; part = part->next)
        {
            n += part->nelts;
        }
    }
    else
    {
        n = 1;
    }

    // 初始化共享内存的链表
    if (ngx_list_init(&cycle->shared_memory, pool, n, sizeof(ngx_shm_zone_t)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 初始化监听数组
    n = old_cycle->listening.nelts ? old_cycle->listening.nelts : 10;

    if (ngx_array_init(&cycle->listening, pool, n, sizeof(ngx_listening_t)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_memzero(cycle->listening.elts, n * sizeof(ngx_listening_t));

    // 初始化重用连接队列
    ngx_queue_init(&cycle->reusable_connections_queue);

    // 分配存储模块配置信息的空间
    cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));
    if (cycle->conf_ctx == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 获取主机名
    if (gethostname(hostname, NGX_MAXHOSTNAMELEN) == -1)
    {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "gethostname() failed");
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 在Linux上，gethostname()会静默截断超过长度的主机名
    hostname[NGX_MAXHOSTNAMELEN - 1] = '\0';
    cycle->hostname.len = ngx_strlen(hostname);

    // 分配存储主机名的空间
    cycle->hostname.data = ngx_pnalloc(pool, cycle->hostname.len);
    if (cycle->hostname.data == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 将主机名转换为小写存储
    ngx_strlow(cycle->hostname.data, (u_char *)hostname, cycle->hostname.len);

    // 加载所有模块
    if (ngx_cycle_modules(cycle) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // 遍历所有模块，初始化模块的配置信息
    for (i = 0; cycle->modules[i]; i++)
    {
        // 只处理核心模块
        if (cycle->modules[i]->type != NGX_CORE_MODULE)
        {
            continue;
        }

        // 获取模块的上下文
        module = cycle->modules[i]->ctx;

        // 调用模块的create_conf函数创建模块的配置信息
        if (module->create_conf)
        {
            rv = module->create_conf(cycle);
            if (rv == NULL)
            {
                ngx_destroy_pool(pool);
                return NULL;
            }
            // 存储模块的配置信息
            cycle->conf_ctx[cycle->modules[i]->index] = rv;
        }
    }

    // 保存环境变量
    senv = environ;

    // 初始化配置结构体
    ngx_memzero(&conf, sizeof(ngx_conf_t));
    conf.args = ngx_array_create(pool, 10, sizeof(ngx_str_t));
    if (conf.args == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }
    conf.temp_pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (conf.temp_pool == NULL)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }
    conf.ctx = cycle->conf_ctx;
    conf.cycle = cycle;
    conf.pool = pool;
    conf.log = log;
    conf.module_type = NGX_CORE_MODULE;
    conf.cmd_type = NGX_MAIN_CONF;

#if 0
    log->log_level = NGX_LOG_DEBUG_ALL;
#endif

    // 解析命令行参数
    if (ngx_conf_param(&conf) != NGX_CONF_OK)
    {
        environ = senv;
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    // 解析配置文件
    if (ngx_conf_parse(&conf, &cycle->conf_file) != NGX_CONF_OK)
    {
        environ = senv;
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    // 测试配置文件语法，并输出结果
    if (ngx_test_config && !ngx_quiet_mode)
    {
        ngx_log_stderr(0, "the configuration file %s syntax is ok",
                       cycle->conf_file.data);
    }

    // 调用核心模块的init_conf函数进行初始化
    for (i = 0; cycle->modules[i]; i++)
    {
        // 只处理核心模块
        if (cycle->modules[i]->type != NGX_CORE_MODULE)
        {
            continue;
        }

        // 获取模块的上下文
        module = cycle->modules[i]->ctx;

        // 调用模块的init_conf函数进行初始化
        if (module->init_conf)
        {
            if (module->init_conf(cycle,
                                  cycle->conf_ctx[cycle->modules[i]->index]) == NGX_CONF_ERROR)
            {
                environ = senv;
                ngx_destroy_cycle_pools(&conf);
                return NULL;
            }
        }
    }

    // 如果是信号处理进程，直接返回
    if (ngx_process == NGX_PROCESS_SIGNALLER)
    {
        return cycle;
    }

    // 获取核心模块的配置结构体
    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ngx_test_config)
    {

        if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK)
        {
            goto failed;
        }
    }
    else if (!ngx_is_init_cycle(old_cycle))
    {

        /*
         * we do not create the pid file in the first ngx_init_cycle() call
         * because we need to write the demonized process pid
         */

        old_ccf = (ngx_core_conf_t *)ngx_get_conf(old_cycle->conf_ctx,
                                                  ngx_core_module);
        if (ccf->pid.len != old_ccf->pid.len || ngx_strcmp(ccf->pid.data, old_ccf->pid.data) != 0)
        {
            /* new pid file name */

            if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK)
            {
                goto failed;
            }

            ngx_delete_pidfile(old_cycle);
        }
    }

    if (ngx_test_lockfile(cycle->lock_file.data, log) != NGX_OK)
    {
        goto failed;
    }

    if (ngx_create_paths(cycle, ccf->user) != NGX_OK)
    {
        goto failed;
    }

    if (ngx_log_open_default(cycle) != NGX_OK)
    {
        goto failed;
    }

    /* open the new files */

    part = &cycle->open_files.part; // 获取打开文件数组的第一个分区
    file = part->elts;              // 获取分区中的文件数组

    for (i = 0; /* void */; i++)
    {

        // 当索引超过当前分区的元素个数时，切换到下一个分区
        if (i >= part->nelts)
        {
            // 如果没有下一个分区，则结束循环
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        // 跳过文件名为空的情况
        if (file[i].name.len == 0)
        {
            continue;
        }

        // 为文件打开或创建一个新的文件描述符
        file[i].fd = ngx_open_file(file[i].name.data,
                                   NGX_FILE_APPEND,
                                   NGX_FILE_CREATE_OR_OPEN,
                                   NGX_FILE_DEFAULT_ACCESS);

        // 记录文件描述符和文件名的调试信息
        ngx_log_debug3(NGX_LOG_DEBUG_CORE, log, 0,
                       "log: %p %d \"%s\"",
                       &file[i], file[i].fd, file[i].name.data);

        // 如果文件描述符无效，则记录错误信息并跳转到failed标签
        if (file[i].fd == NGX_INVALID_FILE)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_open_file_n " \"%s\" failed",
                          file[i].name.data);
            goto failed;
        }

#if !(NGX_WIN32)
        // 设置文件描述符的FD_CLOEXEC标志，确保在执行exec系列函数时关闭文件描述符
        if (fcntl(file[i].fd, F_SETFD, FD_CLOEXEC) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          "fcntl(FD_CLOEXEC) \"%s\" failed",
                          file[i].name.data);
            goto failed;
        }
#endif
    }

    // 设置新的日志指针，用于后续的日志记录
    cycle->log = &cycle->new_log;
    pool->log = &cycle->new_log;

    /* create shared memory 新旧shared_memory链表的比较，相同的共享内存保留，旧的不同的共享内存被释放，新的被创建 */

    /*
     * 遍历共享内存区域数组，为每个共享内存区域执行相应的初始化操作。
     * 如果在旧周期中找到相同名称和大小的共享内存区域，则复用其地址，并调用相应的初始化函数。
     * 如果没有找到匹配的共享内存区域，则分配新的共享内存并执行相应的初始化操作。
     * 如果任何操作失败，则跳转到failed标签。
     */
    part = &cycle->shared_memory.part; // 获取共享内存区域数组的第一个分区
    shm_zone = part->elts;             // 获取分区中的共享内存区域数组

    for (i = 0; /* void */; i++)
    {

        // 当索引超过当前分区的元素个数时，切换到下一个分区
        if (i >= part->nelts)
        {
            // 如果没有下一个分区，则结束循环
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        // 检查共享内存区域的大小是否为零
        if (shm_zone[i].shm.size == 0)
        {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                          "zero size shared memory zone \"%V\"",
                          &shm_zone[i].shm.name);
            goto failed;
        }

        // 设置共享内存区域的日志指针
        shm_zone[i].shm.log = cycle->log;

        // 获取旧周期中的共享内存区域数组
        opart = &old_cycle->shared_memory.part;
        oshm_zone = opart->elts;

        for (n = 0; /* void */; n++)
        {

            // 当索引超过旧周期分区的元素个数时，切换到下一个分区
            if (n >= opart->nelts)
            {
                // 如果没有下一个分区，则结束循环
                if (opart->next == NULL)
                {
                    break;
                }
                opart = opart->next;
                oshm_zone = opart->elts;
                n = 0;
            }

            // 检查共享内存区域名称长度是否相同
            if (shm_zone[i].shm.name.len != oshm_zone[n].shm.name.len)
            {
                continue;
            }

            // 检查共享内存区域名称内容是否相同
            if (ngx_strncmp(shm_zone[i].shm.name.data,
                            oshm_zone[n].shm.name.data,
                            shm_zone[i].shm.name.len) != 0)
            {
                continue;
            }

            // 检查共享内存区域的tag、大小和noreuse标志是否相同
            if (shm_zone[i].tag == oshm_zone[n].tag && shm_zone[i].shm.size == oshm_zone[n].shm.size && !shm_zone[i].noreuse)
            {
                // 复用旧周期中共享内存区域的地址，并调用初始化函数
                shm_zone[i].shm.addr = oshm_zone[n].shm.addr;
#if (NGX_WIN32)
                shm_zone[i].shm.handle = oshm_zone[n].shm.handle;
#endif

                // 调用共享内存区域的初始化函数
                if (shm_zone[i].init(&shm_zone[i], oshm_zone[n].data) != NGX_OK)
                {
                    goto failed;
                }

                // 找到匹配的共享内存区域，跳过当前循环继续处理下一个共享内存区域
                goto shm_zone_found;
            }

            break;
        }

        // 如果没有找到匹配的共享内存区域，则分配新的共享内存并执行相应的初始化操作
        if (ngx_shm_alloc(&shm_zone[i].shm) != NGX_OK)
        {
            goto failed;
        }

        // 初始化共享内存区域的内存池
        if (ngx_init_zone_pool(cycle, &shm_zone[i]) != NGX_OK)
        {
            goto failed;
        }

        // 调用共享内存区域的初始化函数
        if (shm_zone[i].init(&shm_zone[i], NULL) != NGX_OK)
        {
            goto failed;
        }

    shm_zone_found:

        // 继续处理下一个共享内存区域
        continue;
    }

    /*
     * 处理监听套接字：
     * 如果旧周期中有监听套接字，则遍历新周期的监听套接字数组，尝试匹配并复用旧周期的监听套接字信息；
     * 如果没有匹配的监听套接字，则标记为新打开的监听套接字。
     * 如果旧周期中没有监听套接字，则将新周期中所有监听套接字标记为新打开的监听套接字。
     * 最后，尝试打开新的监听套接字，如果失败则跳转到failed标签。
     * 在非测试配置的情况下，配置监听套接字。
     */
    if (old_cycle->listening.nelts)
    {
        // 旧周期中存在监听套接字
        ls = old_cycle->listening.elts;
        for (i = 0; i < old_cycle->listening.nelts; i++)
        {
            ls[i].remain = 0;
        }

        nls = cycle->listening.elts;
        for (n = 0; n < cycle->listening.nelts; n++)
        {
            for (i = 0; i < old_cycle->listening.nelts; i++)
            {
                if (ls[i].ignore)
                {
                    continue;
                }

                if (ls[i].remain)
                {
                    continue;
                }

                if (ls[i].type != nls[n].type)
                {
                    continue;
                }

                if (ngx_cmp_sockaddr(nls[n].sockaddr, nls[n].socklen,
                                     ls[i].sockaddr, ls[i].socklen, 1) == NGX_OK)
                {
                    nls[n].fd = ls[i].fd;
                    nls[n].inherited = ls[i].inherited;
                    nls[n].previous = &ls[i];
                    ls[i].remain = 1;

                    if (ls[i].backlog != nls[n].backlog)
                    {
                        nls[n].listen = 1;
                    }

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
                    nls[n].deferred_accept = ls[i].deferred_accept;

                    if (ls[i].accept_filter && nls[n].accept_filter)
                    {
                        if (ngx_strcmp(ls[i].accept_filter,
                                       nls[n].accept_filter) != 0)
                        {
                            nls[n].delete_deferred = 1;
                            nls[n].add_deferred = 1;
                        }
                    }
                    else if (ls[i].accept_filter)
                    {
                        nls[n].delete_deferred = 1;
                    }
                    else if (nls[n].accept_filter)
                    {
                        nls[n].add_deferred = 1;
                    }
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
                    if (ls[i].deferred_accept && !nls[n].deferred_accept)
                    {
                        nls[n].delete_deferred = 1;
                    }
                    else if (ls[i].deferred_accept != nls[n].deferred_accept)
                    {
                        nls[n].add_deferred = 1;
                    }
#endif

#if (NGX_HAVE_REUSEPORT)
                    if (nls[n].reuseport && !ls[i].reuseport)
                    {
                        nls[n].add_reuseport = 1;
                    }
#endif

                    break;
                }
            }

            if (nls[n].fd == (ngx_socket_t)-1)
            {
                nls[n].open = 1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
                if (nls[n].accept_filter)
                {
                    nls[n].add_deferred = 1;
                }
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
                if (nls[n].deferred_accept)
                {
                    nls[n].add_deferred = 1;
                }
#endif
            }
        }
    }
    else
    {
        // 旧周期中没有监听套接字
        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++)
        {
            ls[i].open = 1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
            if (ls[i].accept_filter)
            {
                ls[i].add_deferred = 1;
            }
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
            if (ls[i].deferred_accept)
            {
                ls[i].add_deferred = 1;
            }
#endif
        }
    }

    // 尝试打开新的监听套接字
    if (ngx_open_listening_sockets(cycle) != NGX_OK)
    {
        goto failed;
    }

    // 非测试配置的情况下，配置监听套接字
    if (!ngx_test_config)
    {
        ngx_configure_listening_sockets(cycle);
    }

    /*
     * 提交新周期的配置：
     * 如果非使用标准错误输出，将日志重定向到新周期的日志对象。
     * 设置内存池的日志对象为新周期的日志对象。
     * 初始化模块，如果失败则调用exit(1)退出程序。
     */
    if (!ngx_use_stderr)
    {
        (void)ngx_log_redirect_stderr(cycle);
    }

    pool->log = cycle->log;

    if (ngx_init_modules(cycle) != NGX_OK)
    {
        /* 初始化模块失败，调用exit(1)退出程序 */
        exit(1);
    }

    /*
     * 关闭和删除旧周期中遗留的资源：
     * 释放不再需要的共享内存。
     */
    opart = &old_cycle->shared_memory.part;
    oshm_zone = opart->elts;

    for (i = 0; /* void */; i++)
    {

        // 当索引超过旧周期分区的元素个数时，切换到下一个分区
        if (i >= opart->nelts)
        {
            // 如果没有下一个分区，则结束循环
            if (opart->next == NULL)
            {
                goto old_shm_zone_done;
            }
            opart = opart->next;
            oshm_zone = opart->elts;
            i = 0;
        }

        // 获取新周期中共享内存区域数组的第一个分区
        part = &cycle->shared_memory.part;
        shm_zone = part->elts;

        for (n = 0; /* void */; n++)
        {

            // 当索引超过新周期分区的元素个数时，切换到下一个分区
            if (n >= part->nelts)
            {
                // 如果没有下一个分区，则结束循环
                if (part->next == NULL)
                {
                    break;
                }
                part = part->next;
                shm_zone = part->elts;
                n = 0;
            }

            // 检查共享内存区域名称长度是否相同
            if (oshm_zone[i].shm.name.len != shm_zone[n].shm.name.len)
            {
                continue;
            }

            // 检查共享内存区域名称内容是否相同
            if (ngx_strncmp(oshm_zone[i].shm.name.data,
                            shm_zone[n].shm.name.data,
                            oshm_zone[i].shm.name.len) != 0)
            {
                continue;
            }

            // 检查共享内存区域的tag、大小和noreuse标志是否相同
            if (oshm_zone[i].tag == shm_zone[n].tag && oshm_zone[i].shm.size == shm_zone[n].shm.size && !oshm_zone[i].noreuse)
            {
                // 找到匹配的共享内存区域，跳过释放操作
                goto live_shm_zone;
            }

            break;
        }

        // 释放不再需要的共享内存
        ngx_shm_free(&oshm_zone[i].shm);

    live_shm_zone:

        // 继续处理下一个共享内存区域
        continue;
    }

old_shm_zone_done:

    /* close the unnecessary listening sockets */

    ls = old_cycle->listening.elts;
    for (i = 0; i < old_cycle->listening.nelts; i++)
    {

        if (ls[i].remain || ls[i].fd == (ngx_socket_t)-1)
        {
            continue;
        }

        if (ngx_close_socket(ls[i].fd) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " listening socket on %V failed",
                          &ls[i].addr_text);
        }

#if (NGX_HAVE_UNIX_DOMAIN)

        if (ls[i].sockaddr->sa_family == AF_UNIX)
        {
            u_char *name;

            name = ls[i].addr_text.data + sizeof("unix:") - 1;

            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "deleting socket %s", name);

            if (ngx_delete_file(name) == NGX_FILE_ERROR)
            {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                              ngx_delete_file_n " %s failed", name);
            }
        }

#endif
    }

    /* close the unnecessary open files */

    part = &old_cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */; i++)
    {

        if (i >= part->nelts)
        {
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr)
        {
            continue;
        }

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    ngx_destroy_pool(conf.temp_pool);

    if (ngx_process == NGX_PROCESS_MASTER || ngx_is_init_cycle(old_cycle))
    {

        ngx_destroy_pool(old_cycle->pool);
        cycle->old_cycle = NULL;

        return cycle;
    }

    if (ngx_temp_pool == NULL)
    {
        ngx_temp_pool = ngx_create_pool(128, cycle->log);
        if (ngx_temp_pool == NULL)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "could not create ngx_temp_pool");
            exit(1);
        }

        n = 10;

        if (ngx_array_init(&ngx_old_cycles, ngx_temp_pool, n,
                           sizeof(ngx_cycle_t *)) != NGX_OK)
        {
            exit(1);
        }

        ngx_memzero(ngx_old_cycles.elts, n * sizeof(ngx_cycle_t *));

        ngx_cleaner_event.handler = ngx_clean_old_cycles;
        ngx_cleaner_event.log = cycle->log;
        ngx_cleaner_event.data = &dumb;
        dumb.fd = (ngx_socket_t)-1;
    }

    ngx_temp_pool->log = cycle->log;

    old = ngx_array_push(&ngx_old_cycles);
    if (old == NULL)
    {
        exit(1);
    }
    *old = old_cycle;

    if (!ngx_cleaner_event.timer_set)
    {
        ngx_add_timer(&ngx_cleaner_event, 30000);
        ngx_cleaner_event.timer_set = 1;
    }

    return cycle;

failed:

    if (!ngx_is_init_cycle(old_cycle))
    {
        old_ccf = (ngx_core_conf_t *)ngx_get_conf(old_cycle->conf_ctx,
                                                  ngx_core_module);
        if (old_ccf->environment)
        {
            environ = old_ccf->environment;
        }
    }

    /*
     * 回滚新周期的配置：
     * 关闭新周期中已打开的文件描述符。
     */
    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */; i++)
    {

        // 当索引超过当前分区的元素个数时，切换到下一个分区
        if (i >= part->nelts)
        {
            // 如果没有下一个分区，则结束循环
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        // 跳过无效的文件描述符以及标准错误输出文件描述符
        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr)
        {
            continue;
        }

        // 尝试关闭文件描述符
        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    /*
     * 释放新周期中不再需要的共享内存：
     * 遍历新周期中的共享内存数组，释放那些在旧周期中没有匹配项的共享内存。
     * 如果是测试配置，则销毁周期池并返回NULL。
     * 关闭新周期中打开的监听套接字，并销毁周期池。
     */
    part = &cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */; i++)
    {

        // 当索引超过当前分区的元素个数时，切换到下一个分区
        if (i >= part->nelts)
        {
            // 如果没有下一个分区，则结束循环
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        // 跳过共享内存地址为NULL的情况
        if (shm_zone[i].shm.addr == NULL)
        {
            continue;
        }

        // 获取旧周期中的共享内存数组
        opart = &old_cycle->shared_memory.part;
        oshm_zone = opart->elts;

        for (n = 0; /* void */; n++)
        {

            // 当索引超过旧周期分区的元素个数时，切换到下一个分区
            if (n >= opart->nelts)
            {
                // 如果没有下一个分区，则结束循环
                if (opart->next == NULL)
                {
                    break;
                }
                opart = opart->next;
                oshm_zone = opart->elts;
                n = 0;
            }

            // 检查共享内存区域名称长度是否相同
            if (shm_zone[i].shm.name.len != oshm_zone[n].shm.name.len)
            {
                continue;
            }

            // 检查共享内存区域名称内容是否相同
            if (ngx_strncmp(shm_zone[i].shm.name.data,
                            oshm_zone[n].shm.name.data,
                            shm_zone[i].shm.name.len) != 0)
            {
                continue;
            }

            // 检查共享内存区域的tag、大小和noreuse标志是否相同
            if (shm_zone[i].tag == oshm_zone[n].tag && shm_zone[i].shm.size == oshm_zone[n].shm.size && !shm_zone[i].noreuse)
            {
                // 找到匹配的共享内存区域，跳过释放操作
                goto old_shm_zone_found;
            }

            break;
        }

        // 释放不再需要的共享内存
        ngx_shm_free(&shm_zone[i].shm);

    old_shm_zone_found:

        // 继续处理下一个共享内存区域
        continue;
    }

    // 如果是测试配置，则销毁周期池并返回NULL
    if (ngx_test_config)
    {
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    // 关闭新周期中打开的监听套接字
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++)
    {
        if (ls[i].fd == (ngx_socket_t)-1 || !ls[i].open)
        {
            continue;
        }

        // 尝试关闭监听套接字
        if (ngx_close_socket(ls[i].fd) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " %V failed",
                          &ls[i].addr_text);
        }
    }

    // 销毁周期池
    ngx_destroy_cycle_pools(&conf);

    // 返回NULL表示回滚配置完成
    return NULL;
}

static void
ngx_destroy_cycle_pools(ngx_conf_t *conf)
{
    ngx_destroy_pool(conf->temp_pool);
    ngx_destroy_pool(conf->pool);
}

static ngx_int_t
ngx_init_zone_pool(ngx_cycle_t *cycle, ngx_shm_zone_t *zn)
{
    u_char *file;
    ngx_slab_pool_t *sp;

    sp = (ngx_slab_pool_t *)zn->shm.addr;

    if (zn->shm.exists)
    {

        if (sp == sp->addr)
        {
            return NGX_OK;
        }

#if (NGX_WIN32)

        /* remap at the required address */

        if (ngx_shm_remap(&zn->shm, sp->addr) != NGX_OK)
        {
            return NGX_ERROR;
        }

        sp = (ngx_slab_pool_t *)zn->shm.addr;

        if (sp == sp->addr)
        {
            return NGX_OK;
        }

#endif

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "shared zone \"%V\" has no equal addresses: %p vs %p",
                      &zn->shm.name, sp->addr, sp);
        return NGX_ERROR;
    }

    sp->end = zn->shm.addr + zn->shm.size;
    sp->min_shift = 3;
    sp->addr = zn->shm.addr;

#if (NGX_HAVE_ATOMIC_OPS)

    file = NULL;

#else

    file = ngx_pnalloc(cycle->pool,
                       cycle->lock_file.len + zn->shm.name.len + 1);
    if (file == NULL)
    {
        return NGX_ERROR;
    }

    (void)ngx_sprintf(file, "%V%V%Z", &cycle->lock_file, &zn->shm.name);

#endif

    if (ngx_shmtx_create(&sp->mutex, &sp->lock, file) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_slab_init(sp);

    return NGX_OK;
}

/**
 * @brief 创建PID文件并写入当前进程的PID
 *
 * @param name PID文件的路径和名称
 * @param log 日志结构体指针
 * @return 返回NGX_OK表示成功，返回NGX_ERROR表示失败
 */
ngx_int_t
ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log)
{
    size_t len;
    ngx_int_t rc;
    ngx_uint_t create;
    ngx_file_t file;
    u_char pid[NGX_INT64_LEN + 2];

    // 如果进程类型大于主进程，直接返回成功
    if (ngx_process > NGX_PROCESS_MASTER)
    {
        return NGX_OK;
    }

    // 初始化文件结构体
    ngx_memzero(&file, sizeof(ngx_file_t));

    // 设置文件名和日志
    file.name = *name;
    file.log = log;

    // 根据测试配置的标志选择文件创建方式
    create = ngx_test_config ? NGX_FILE_CREATE_OR_OPEN : NGX_FILE_TRUNCATE;

    // 打开PID文件，获取文件描述符
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDWR,
                            create, NGX_FILE_DEFAULT_ACCESS);

    // 如果打开文件失败，记录错误日志并返回失败
    if (file.fd == NGX_INVALID_FILE)
    {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return NGX_ERROR;
    }

    // 初始化返回值为成功
    rc = NGX_OK;

    // 如果不是测试配置，将当前进程的PID写入PID文件
    if (!ngx_test_config)
    {
        len = ngx_snprintf(pid, NGX_INT64_LEN + 2, "%P%N", ngx_pid) - pid;

        if (ngx_write_file(&file, pid, len, 0) == NGX_ERROR)
        {
            rc = NGX_ERROR;
        }
    }

    // 关闭文件
    if (ngx_close_file(file.fd) == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }

    return rc;
}

void ngx_delete_pidfile(ngx_cycle_t *cycle)
{
    u_char *name;
    ngx_core_conf_t *ccf;

    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    name = ngx_new_binary ? ccf->oldpid.data : ccf->pid.data;

    if (ngx_delete_file(name) == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }
}

/**
 * @brief 处理信号并向指定的进程发送信号
 *
 * @param cycle NGX的全局cycle结构体
 * @param sig 待发送的信号
 * @return 返回1表示出错，返回0表示成功
 */
ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    ssize_t n;
    ngx_pid_t pid;
    ngx_file_t file;
    ngx_core_conf_t *ccf;
    u_char buf[NGX_INT64_LEN + 2];

    // 打印日志，表示信号处理进程已启动
    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "signal process started");

    // 获取core模块的配置信息
    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // 初始化文件结构体
    ngx_memzero(&file, sizeof(ngx_file_t));

    // 设置文件名和日志
    file.name = ccf->pid;
    file.log = cycle->log;

    // 打开PID文件，读取其中的进程ID
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY,
                            NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);

    // 如果打开文件失败，记录错误日志并返回1
    if (file.fd == NGX_INVALID_FILE)
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return 1;
    }

    // 读取PID文件内容到缓冲区
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);

    // 关闭文件
    if (ngx_close_file(file.fd) == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }

    // 如果读取文件失败，返回1
    if (n == NGX_ERROR)
    {
        return 1;
    }

    // 去除缓冲区末尾的回车换行符
    while (n-- && (buf[n] == CR || buf[n] == LF))
    { /* void */
    }

    // 将缓冲区内容转换为进程ID
    pid = ngx_atoi(buf, ++n);

    // 如果转换失败，记录错误日志并返回1
    if (pid == (ngx_pid_t)NGX_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "invalid PID number \"%*s\" in \"%s\"",
                      n, buf, file.name.data);
        return 1;
    }

    // 调用操作系统相关的函数向指定的进程发送信号
    return ngx_os_signal_process(cycle, sig, pid);
}

/*
 * 测试文件锁定功能，用于检测系统是否支持原子操作。
 * 如果不支持原子操作，则尝试打开一个文件，关闭它，并删除它，以验证文件系统支持锁定。
 * 如果文件操作失败，则记录错误信息，并返回NGX_ERROR；否则返回NGX_OK。
 */
static ngx_int_t
ngx_test_lockfile(u_char *file, ngx_log_t *log)
{
#if !(NGX_HAVE_ATOMIC_OPS)

    // 尝试以读写模式、创建或打开、默认访问权限的方式打开文件
    ngx_fd_t fd = ngx_open_file(file, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                                NGX_FILE_DEFAULT_ACCESS);

    // 检查文件打开是否成功
    if (fd == NGX_INVALID_FILE)
    {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file);
        return NGX_ERROR;
    }

    // 尝试关闭文件
    if (ngx_close_file(fd) == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file);
    }

    // 尝试删除文件
    if (ngx_delete_file(file) == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", file);
    }

#endif

    return NGX_OK;
}

void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user)
{
    ngx_fd_t fd;
    ngx_uint_t i;
    ngx_list_part_t *part;
    ngx_open_file_t *file;

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */; i++)
    {

        if (i >= part->nelts)
        {
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].name.len == 0)
        {
            continue;
        }

        if (file[i].flush)
        {
            file[i].flush(&file[i], cycle->log);
        }

        fd = ngx_open_file(file[i].name.data, NGX_FILE_APPEND,
                           NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "reopen file \"%s\", old:%d new:%d",
                       file[i].name.data, file[i].fd, fd);

        if (fd == NGX_INVALID_FILE)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          ngx_open_file_n " \"%s\" failed", file[i].name.data);
            continue;
        }

#if !(NGX_WIN32)
        if (user != (ngx_uid_t)NGX_CONF_UNSET_UINT)
        {
            ngx_file_info_t fi;

            if (ngx_file_info(file[i].name.data, &fi) == NGX_FILE_ERROR)
            {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              ngx_file_info_n " \"%s\" failed",
                              file[i].name.data);

                if (ngx_close_file(fd) == NGX_FILE_ERROR)
                {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  ngx_close_file_n " \"%s\" failed",
                                  file[i].name.data);
                }

                continue;
            }

            if (fi.st_uid != user)
            {
                if (chown((const char *)file[i].name.data, user, -1) == -1)
                {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  "chown(\"%s\", %d) failed",
                                  file[i].name.data, user);

                    if (ngx_close_file(fd) == NGX_FILE_ERROR)
                    {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                      ngx_close_file_n " \"%s\" failed",
                                      file[i].name.data);
                    }

                    continue;
                }
            }

            if ((fi.st_mode & (S_IRUSR | S_IWUSR)) != (S_IRUSR | S_IWUSR))
            {

                fi.st_mode |= (S_IRUSR | S_IWUSR);

                if (chmod((const char *)file[i].name.data, fi.st_mode) == -1)
                {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  "chmod() \"%s\" failed", file[i].name.data);

                    if (ngx_close_file(fd) == NGX_FILE_ERROR)
                    {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                      ngx_close_file_n " \"%s\" failed",
                                      file[i].name.data);
                    }

                    continue;
                }
            }
        }

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) \"%s\" failed",
                          file[i].name.data);

            if (ngx_close_file(fd) == NGX_FILE_ERROR)
            {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              ngx_close_file_n " \"%s\" failed",
                              file[i].name.data);
            }

            continue;
        }
#endif

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }

        file[i].fd = fd;
    }

    (void)ngx_log_redirect_stderr(cycle);
}

ngx_shm_zone_t *
ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name, size_t size, void *tag)
{
    ngx_uint_t i;
    ngx_shm_zone_t *shm_zone;
    ngx_list_part_t *part;

    part = &cf->cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */; i++)
    {

        if (i >= part->nelts)
        {
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (name->len != shm_zone[i].shm.name.len)
        {
            continue;
        }

        if (ngx_strncmp(name->data, shm_zone[i].shm.name.data, name->len) != 0)
        {
            continue;
        }

        if (tag != shm_zone[i].tag)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "the shared memory zone \"%V\" is "
                               "already declared for a different use",
                               &shm_zone[i].shm.name);
            return NULL;
        }

        if (shm_zone[i].shm.size == 0)
        {
            shm_zone[i].shm.size = size;
        }

        if (size && size != shm_zone[i].shm.size)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "the size %uz of shared memory zone \"%V\" "
                               "conflicts with already declared size %uz",
                               size, &shm_zone[i].shm.name, shm_zone[i].shm.size);
            return NULL;
        }

        return &shm_zone[i];
    }

    shm_zone = ngx_list_push(&cf->cycle->shared_memory);

    if (shm_zone == NULL)
    {
        return NULL;
    }

    shm_zone->data = NULL;
    shm_zone->shm.log = cf->cycle->log;
    shm_zone->shm.addr = NULL;
    shm_zone->shm.size = size;
    shm_zone->shm.name = *name;
    shm_zone->shm.exists = 0;
    shm_zone->init = NULL;
    shm_zone->tag = tag;
    shm_zone->noreuse = 0;

    return shm_zone;
}

static void
ngx_clean_old_cycles(ngx_event_t *ev)
{
    ngx_uint_t i, n, found, live;
    ngx_log_t *log;
    ngx_cycle_t **cycle;

    log = ngx_cycle->log;
    ngx_temp_pool->log = log;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycles");

    live = 0;

    cycle = ngx_old_cycles.elts;
    for (i = 0; i < ngx_old_cycles.nelts; i++)
    {

        if (cycle[i] == NULL)
        {
            continue;
        }

        found = 0;

        for (n = 0; n < cycle[i]->connection_n; n++)
        {
            if (cycle[i]->connections[n].fd != (ngx_socket_t)-1)
            {
                found = 1;

                ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "live fd:%ui", n);

                break;
            }
        }

        if (found)
        {
            live = 1;
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycle: %ui", i);

        ngx_destroy_pool(cycle[i]->pool);
        cycle[i] = NULL;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "old cycles status: %ui", live);

    if (live)
    {
        ngx_add_timer(ev, 30000);
    }
    else
    {
        ngx_destroy_pool(ngx_temp_pool);
        ngx_temp_pool = NULL;
        ngx_old_cycles.nelts = 0;
    }
}

void ngx_set_shutdown_timer(ngx_cycle_t *cycle)
{
    ngx_core_conf_t *ccf;

    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ccf->shutdown_timeout)
    {
        ngx_shutdown_event.handler = ngx_shutdown_timer_handler;
        ngx_shutdown_event.data = cycle;
        ngx_shutdown_event.log = cycle->log;
        ngx_shutdown_event.cancelable = 1;

        ngx_add_timer(&ngx_shutdown_event, ccf->shutdown_timeout);
    }
}

static void
ngx_shutdown_timer_handler(ngx_event_t *ev)
{
    ngx_uint_t i;
    ngx_cycle_t *cycle;
    ngx_connection_t *c;

    cycle = ev->data;

    c = cycle->connections;

    for (i = 0; i < cycle->connection_n; i++)
    {

        if (c[i].fd == (ngx_socket_t)-1 || c[i].read == NULL || c[i].read->accept || c[i].read->channel || c[i].read->resolver)
        {
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "*%uA shutdown timeout", c[i].number);

        c[i].close = 1;
        c[i].error = 1;

        c[i].read->handler(c[i].read);
    }
}
