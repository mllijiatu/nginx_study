
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>

static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n,
                                       ngx_int_t type);
static void ngx_start_cache_manager_processes(ngx_cycle_t *cycle,
                                              ngx_uint_t respawn);
static void ngx_pass_open_channel(ngx_cycle_t *cycle);
static void ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo);
static ngx_uint_t ngx_reap_children(ngx_cycle_t *cycle);
static void ngx_master_process_exit(ngx_cycle_t *cycle);
static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker);
static void ngx_worker_process_exit(ngx_cycle_t *cycle);
static void ngx_channel_handler(ngx_event_t *ev);
static void ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_cache_manager_process_handler(ngx_event_t *ev);
static void ngx_cache_loader_process_handler(ngx_event_t *ev);

ngx_uint_t ngx_process;
ngx_uint_t ngx_worker;
ngx_pid_t ngx_pid;
ngx_pid_t ngx_parent;

sig_atomic_t ngx_reap;
sig_atomic_t ngx_sigio;
sig_atomic_t ngx_sigalrm;
sig_atomic_t ngx_terminate;
sig_atomic_t ngx_quit;
sig_atomic_t ngx_debug_quit;
ngx_uint_t ngx_exiting;
sig_atomic_t ngx_reconfigure;
sig_atomic_t ngx_reopen;

sig_atomic_t ngx_change_binary;
ngx_pid_t ngx_new_binary;
ngx_uint_t ngx_inherited;
ngx_uint_t ngx_daemonized;

sig_atomic_t ngx_noaccept;
ngx_uint_t ngx_noaccepting;
ngx_uint_t ngx_restart;

static u_char master_process[] = "master process";

static ngx_cache_manager_ctx_t ngx_cache_manager_ctx = {
    ngx_cache_manager_process_handler, "cache manager process", 0};

static ngx_cache_manager_ctx_t ngx_cache_loader_ctx = {
    ngx_cache_loader_process_handler, "cache loader process", 60000};

static ngx_cycle_t ngx_exit_cycle;
static ngx_log_t ngx_exit_log;
static ngx_open_file_t ngx_exit_log_file;

/*
 * ngx_master_process_cycle - 主进程循环
 *
 * 该函数是Nginx主进程的核心循环，负责处理信号、管理子进程、周期性任务等。
 *
 * 主要工作包括：
 *   1. 初始化信号集合，阻塞相关信号。
 *   2. 构造进程标题并设置进程标题。
 *   3. 启动工作进程和缓存管理进程。
 *   4. 处理定时器，定期执行特定任务。
 *   5. 阻塞等待信号并处理子进程退出。
 *   6. 根据信号执行相应的操作，如终止、退出、重新加载配置、重启等。
 *   7. 处理日志重开、二进制切换等操作。
 *   8. 循环执行上述步骤，保持主进程一直运行。
 *
 * 参数:
 *     cycle - Nginx循环结构体，包含配置信息和运行时状态
 */
void ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    char *title;          // 进程标题
    u_char *p;            // 临时指针
    size_t size;          // 进程标题长度
    ngx_int_t i;          // 循环计数器
    ngx_uint_t sigio;     // 标记是否有IO信号
    sigset_t set;         // 信号集合
    struct itimerval itv; // 定时器结构体
    ngx_uint_t live;      // 子进程是否存活标志
    ngx_msec_t delay;     // 定时器延迟时间
    ngx_core_conf_t *ccf; // core模块配置结构体

    // 初始化信号集合
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGINT);
    sigaddset(&set, ngx_signal_value(NGX_RECONFIGURE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_REOPEN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_NOACCEPT_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_CHANGEBIN_SIGNAL));

    // 阻塞信号集合
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    // 清空信号集合
    sigemptyset(&set);

    // 计算进程标题长度
    size = sizeof(master_process);
    for (i = 0; i < ngx_argc; i++)
    {
        size += ngx_strlen(ngx_argv[i]) + 1;
    }

    // 分配进程标题内存
    title = ngx_pnalloc(cycle->pool, size);
    if (title == NULL)
    {
        /* fatal */
        exit(2);
    }

    // 构造进程标题
    p = ngx_cpymem(title, master_process, sizeof(master_process) - 1);
    for (i = 0; i < ngx_argc; i++)
    {
        *p++ = ' ';
        p = ngx_cpystrn(p, (u_char *)ngx_argv[i], size);
    }

    // 设置进程标题
    ngx_setproctitle(title);

    // 获取core模块配置结构体
    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // 启动工作进程和缓存管理进程
    ngx_start_worker_processes(cycle, ccf->worker_processes, NGX_PROCESS_RESPAWN);
    ngx_start_cache_manager_processes(cycle, 0);

    ngx_new_binary = 0;
    delay = 0;
    sigio = 0;
    live = 1;

    for (;;)
    {
        // 处理定时器
        if (delay)
        {
            if (ngx_sigalrm)
            {
                sigio = 0;
                delay *= 2;
                ngx_sigalrm = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "termination cycle: %M", delay);

            itv.it_interval.tv_sec = 0;
            itv.it_interval.tv_usec = 0;
            itv.it_value.tv_sec = delay / 1000;
            itv.it_value.tv_usec = (delay % 1000) * 1000;

            // 设置定时器
            if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setitimer() failed");
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "sigsuspend");

        // 阻塞等待信号
        sigsuspend(&set);

        // 更新时间
        ngx_time_update();

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "wake up, sigio %i", sigio);

        // 处理子进程退出
        if (ngx_reap)
        {
            ngx_reap = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reap children");

            live = ngx_reap_children(cycle);
        }

        // 主进程退出条件判断
        if (!live && (ngx_terminate || ngx_quit))
        {
            ngx_master_process_exit(cycle);
        }

        // 处理终止信号
        if (ngx_terminate)
        {
            if (delay == 0)
            {
                delay = 50;
            }

            if (sigio)
            {
                sigio--;
                continue;
            }

            sigio = ccf->worker_processes + 2 /* cache processes */;

            if (delay > 1000)
            {
                ngx_signal_worker_processes(cycle, SIGKILL);
            }
            else
            {
                ngx_signal_worker_processes(cycle,
                                            ngx_signal_value(NGX_TERMINATE_SIGNAL));
            }

            continue;
        }

        // 处理退出信号
        if (ngx_quit)
        {
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
            ngx_close_listening_sockets(cycle);

            continue;
        }

        // 处理重新加载配置信号
        if (ngx_reconfigure)
        {
            ngx_reconfigure = 0;

            if (ngx_new_binary)
            {
                ngx_start_worker_processes(cycle, ccf->worker_processes,
                                           NGX_PROCESS_RESPAWN);
                ngx_start_cache_manager_processes(cycle, 0);
                ngx_noaccepting = 0;

                continue;
            }

            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL)
            {
                cycle = (ngx_cycle_t *)ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;
            ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx,
                                                  ngx_core_module);
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_JUST_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 1);

            /* allow new processes to start */
            ngx_msleep(100);

            live = 1;
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }

        // 处理重启信号
        if (ngx_restart)
        {
            ngx_restart = 0;
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 0);
            live = 1;
        }

        // 处理重新打开日志文件信号
        if (ngx_reopen)
        {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, ccf->user);
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_REOPEN_SIGNAL));
        }

        // 处理切换二进制文件信号
        if (ngx_change_binary)
        {
            ngx_change_binary = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "changing binary");
            ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv);
        }

        // 处理停止接受新连接信号
        if (ngx_noaccept)
        {
            ngx_noaccept = 0;
            ngx_noaccepting = 1;
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }
    }
}

/*
 * ngx_start_worker_processes - 启动工作进程
 *
 * 该函数用于启动指定数量的工作进程，每个工作进程都执行 ngx_worker_process_cycle 函数。
 * 启动后，还通过 ngx_pass_open_channel 函数传递打开的通道给子进程。
 *
 * 参数:
 *     cycle - Nginx循环结构体，包含配置信息和运行时状态
 *     n - 启动的工作进程数量
 *     type - 进程类型，例如 NGX_PROCESS_RESPAWN
 */
static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type)
{
    ngx_int_t i;

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");

    // 循环启动工作进程
    for (i = 0; i < n; i++)
    {
        // 使用 ngx_spawn_process 函数启动工作进程，并指定执行函数为 ngx_worker_process_cycle
        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *)(intptr_t)i, "worker process", type);

        // 传递打开的通道给子进程
        ngx_pass_open_channel(cycle);
    }
}

static void
ngx_start_cache_manager_processes(ngx_cycle_t *cycle, ngx_uint_t respawn)
{
    ngx_uint_t i, manager, loader;
    ngx_path_t **path;

    manager = 0;
    loader = 0;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++)
    {

        if (path[i]->manager)
        {
            manager = 1;
        }

        if (path[i]->loader)
        {
            loader = 1;
        }
    }

    if (manager == 0)
    {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_manager_ctx, "cache manager process",
                      respawn ? NGX_PROCESS_JUST_RESPAWN : NGX_PROCESS_RESPAWN);

    ngx_pass_open_channel(cycle);

    if (loader == 0)
    {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_loader_ctx, "cache loader process",
                      respawn ? NGX_PROCESS_JUST_SPAWN : NGX_PROCESS_NORESPAWN);

    ngx_pass_open_channel(cycle);
}

/*
 * ngx_pass_open_channel - 传递打开的通道给其他进程
 *
 * 该函数用于向其他进程传递打开的通道，以便建立通信。通过 ngx_write_channel 函数将通道信息发送给其他进程。
 *
 * 参数:
 *     cycle - Nginx循环结构体，包含配置信息和运行时状态
 */
static void ngx_pass_open_channel(ngx_cycle_t *cycle)
{
    ngx_int_t i;
    ngx_channel_t ch;

    // 初始化通道结构体
    ngx_memzero(&ch, sizeof(ngx_channel_t));

    // 设置通道命令为打开通道
    ch.command = NGX_CMD_OPEN_CHANNEL;
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];

    // 遍历其他进程，向它们传递通道信息
    for (i = 0; i < ngx_last_process; i++)
    {
        if (i == ngx_process_slot || ngx_processes[i].pid == -1 || ngx_processes[i].channel[0] == -1)
        {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "pass channel s:%i pid:%P fd:%d to s:%i pid:%P fd:%d",
                       ch.slot, ch.pid, ch.fd,
                       i, ngx_processes[i].pid,
                       ngx_processes[i].channel[0]);

        // 通过 ngx_write_channel 函数向其他进程发送通道信息
        ngx_write_channel(ngx_processes[i].channel[0],
                          &ch, sizeof(ngx_channel_t), cycle->log);
    }
}

static void
ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo)
{
    ngx_int_t i;
    ngx_err_t err;
    ngx_channel_t ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

#if (NGX_BROKEN_SCM_RIGHTS)

    ch.command = 0;

#else

    switch (signo)
    {

    case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
        ch.command = NGX_CMD_QUIT;
        break;

    case ngx_signal_value(NGX_TERMINATE_SIGNAL):
        ch.command = NGX_CMD_TERMINATE;
        break;

    case ngx_signal_value(NGX_REOPEN_SIGNAL):
        ch.command = NGX_CMD_REOPEN;
        break;

    default:
        ch.command = 0;
    }

#endif

    ch.fd = -1;

    for (i = 0; i < ngx_last_process; i++)
    {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].detached || ngx_processes[i].pid == -1)
        {
            continue;
        }

        if (ngx_processes[i].just_spawn)
        {
            ngx_processes[i].just_spawn = 0;
            continue;
        }

        if (ngx_processes[i].exiting && signo == ngx_signal_value(NGX_SHUTDOWN_SIGNAL))
        {
            continue;
        }

        if (ch.command)
        {
            if (ngx_write_channel(ngx_processes[i].channel[0],
                                  &ch, sizeof(ngx_channel_t), cycle->log) == NGX_OK)
            {
                if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL))
                {
                    ngx_processes[i].exiting = 1;
                }

                continue;
            }
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "kill (%P, %d)", ngx_processes[i].pid, signo);

        if (kill(ngx_processes[i].pid, signo) == -1)
        {
            err = ngx_errno;
            ngx_log_error(NGX_LOG_ALERT, cycle->log, err,
                          "kill(%P, %d) failed", ngx_processes[i].pid, signo);

            if (err == NGX_ESRCH)
            {
                ngx_processes[i].exited = 1;
                ngx_processes[i].exiting = 0;
                ngx_reap = 1;
            }

            continue;
        }

        if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL))
        {
            ngx_processes[i].exiting = 1;
        }
    }
}

static ngx_uint_t
ngx_reap_children(ngx_cycle_t *cycle)
{
    ngx_int_t i, n;
    ngx_uint_t live;
    ngx_channel_t ch;
    ngx_core_conf_t *ccf;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_CLOSE_CHANNEL;
    ch.fd = -1;

    live = 0;
    for (i = 0; i < ngx_last_process; i++)
    {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].pid == -1)
        {
            continue;
        }

        if (ngx_processes[i].exited)
        {

            if (!ngx_processes[i].detached)
            {
                ngx_close_channel(ngx_processes[i].channel, cycle->log);

                ngx_processes[i].channel[0] = -1;
                ngx_processes[i].channel[1] = -1;

                ch.pid = ngx_processes[i].pid;
                ch.slot = i;

                for (n = 0; n < ngx_last_process; n++)
                {
                    if (ngx_processes[n].exited || ngx_processes[n].pid == -1 || ngx_processes[n].channel[0] == -1)
                    {
                        continue;
                    }

                    ngx_log_debug3(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                   "pass close channel s:%i pid:%P to:%P",
                                   ch.slot, ch.pid, ngx_processes[n].pid);

                    /* TODO: NGX_AGAIN */

                    ngx_write_channel(ngx_processes[n].channel[0],
                                      &ch, sizeof(ngx_channel_t), cycle->log);
                }
            }

            if (ngx_processes[i].respawn && !ngx_processes[i].exiting && !ngx_terminate && !ngx_quit)
            {
                if (ngx_spawn_process(cycle, ngx_processes[i].proc,
                                      ngx_processes[i].data,
                                      ngx_processes[i].name, i) == NGX_INVALID_PID)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                                  "could not respawn %s",
                                  ngx_processes[i].name);
                    continue;
                }

                ngx_pass_open_channel(cycle);

                live = 1;

                continue;
            }

            if (ngx_processes[i].pid == ngx_new_binary)
            {

                ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx,
                                                      ngx_core_module);

                if (ngx_rename_file((char *)ccf->oldpid.data,
                                    (char *)ccf->pid.data) == NGX_FILE_ERROR)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                                  ngx_rename_file_n " %s back to %s failed "
                                                    "after the new binary process \"%s\" exited",
                                  ccf->oldpid.data, ccf->pid.data, ngx_argv[0]);
                }

                ngx_new_binary = 0;
                if (ngx_noaccepting)
                {
                    ngx_restart = 1;
                    ngx_noaccepting = 0;
                }
            }

            if (i == ngx_last_process - 1)
            {
                ngx_last_process--;
            }
            else
            {
                ngx_processes[i].pid = -1;
            }
        }
        else if (ngx_processes[i].exiting || !ngx_processes[i].detached)
        {
            live = 1;
        }
    }

    return live;
}

static void
ngx_master_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t i;

    ngx_delete_pidfile(cycle);

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exit");

    for (i = 0; cycle->modules[i]; i++)
    {
        if (cycle->modules[i]->exit_master)
        {
            cycle->modules[i]->exit_master(cycle);
        }
    }

    ngx_close_listening_sockets(cycle);

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */

    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    exit(0);
}

/*
 * 描述：这个函数表示NGINX服务器中工作进程的主循环。
 *
 * 参数：
 *   - cycle：指向ngx_cycle_t结构的指针，表示当前周期。
 *   - data：指向与工作进程相关的数据的指针，本例中为工作进程的索引。
 *
 * 返回：此函数不返回任何值。
 */

static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_int_t worker = (intptr_t)data;

    ngx_process = NGX_PROCESS_WORKER; // 设置进程类型为工作进程。
    ngx_worker = worker;              // 设置当前工作进程的索引。

    ngx_worker_process_init(cycle, worker); // 初始化工作进程。

    ngx_setproctitle("worker process"); // 设置进程标题以进行识别。

    for (;;)
    {
        // 检查服务器是否正在退出且没有剩余计时器。
        if (ngx_exiting)
        {
            if (ngx_event_no_timers_left() == NGX_OK)
            {
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
                ngx_worker_process_exit(cycle); // 退出工作进程。
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events_and_timers(cycle); // 处理事件和计时器。

        // 检查是否收到终止信号。
        if (ngx_terminate)
        {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            ngx_worker_process_exit(cycle); // 退出工作进程。
        }

        // 检查是否收到优雅关闭信号。
        if (ngx_quit)
        {
            ngx_quit = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "gracefully shutting down");
            ngx_setproctitle("worker process is shutting down");

            // 启动优雅关闭过程。
            if (!ngx_exiting)
            {
                ngx_exiting = 1;
                ngx_set_shutdown_timer(cycle);
                ngx_close_listening_sockets(cycle);
                ngx_close_idle_connections(cycle);
                ngx_event_process_posted(cycle, &ngx_posted_events);
            }
        }

        // 检查是否需要重新打开日志。
        if (ngx_reopen)
        {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1); // 重新打开日志文件。
        }
    }
}

/*
 * 描述：初始化工作进程。
 *
 * 参数：
 *   - cycle：指向ngx_cycle_t结构的指针，表示当前周期。
 *   - worker：工作进程的索引。
 *
 * 返回：此函数不返回任何值。
 */

static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    sigset_t set;
    ngx_int_t n;
    ngx_time_t *tp;
    ngx_uint_t i;
    ngx_cpuset_t *cpu_affinity;
    struct rlimit rlmt;
    ngx_core_conf_t *ccf;

    if (ngx_set_environment(cycle, NULL) == NULL)
    {
        /* 致命错误 */
        exit(2);
    }

    ccf = (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // 设置进程优先级
    if (worker >= 0 && ccf->priority != 0)
    {
        if (setpriority(PRIO_PROCESS, 0, ccf->priority) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setpriority(%d) failed", ccf->priority);
        }
    }

    // 设置文件描述符数限制
    if (ccf->rlimit_nofile != NGX_CONF_UNSET)
    {
        rlmt.rlim_cur = (rlim_t)ccf->rlimit_nofile;
        rlmt.rlim_max = (rlim_t)ccf->rlimit_nofile;

        if (setrlimit(RLIMIT_NOFILE, &rlmt) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_NOFILE, %i) failed",
                          ccf->rlimit_nofile);
        }
    }

    // 设置核心转储限制
    if (ccf->rlimit_core != NGX_CONF_UNSET)
    {
        rlmt.rlim_cur = (rlim_t)ccf->rlimit_core;
        rlmt.rlim_max = (rlim_t)ccf->rlimit_core;

        if (setrlimit(RLIMIT_CORE, &rlmt) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_CORE, %O) failed",
                          ccf->rlimit_core);
        }
    }

    // 如果是超级用户，设置用户和组
    if (geteuid() == 0)
    {
        if (setgid(ccf->group) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setgid(%d) failed", ccf->group);
            /* 致命错误 */
            exit(2);
        }

        if (initgroups(ccf->username, ccf->group) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "initgroups(%s, %d) failed",
                          ccf->username, ccf->group);
        }

#if (NGX_HAVE_PR_SET_KEEPCAPS && NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user)
        {
            if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1)
            {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "prctl(PR_SET_KEEPCAPS, 1) failed");
                /* 致命错误 */
                exit(2);
            }
        }
#endif

        if (setuid(ccf->user) == -1)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setuid(%d) failed", ccf->user);
            /* 致命错误 */
            exit(2);
        }

#if (NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user)
        {
            struct __user_cap_data_struct data;
            struct __user_cap_header_struct header;

            ngx_memzero(&header, sizeof(struct __user_cap_header_struct));
            ngx_memzero(&data, sizeof(struct __user_cap_data_struct));

            header.version = _LINUX_CAPABILITY_VERSION_1;
            data.effective = CAP_TO_MASK(CAP_NET_RAW);
            data.permitted = data.effective;

            if (syscall(SYS_capset, &header, &data) == -1)
            {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "capset() failed");
                /* 致命错误 */
                exit(2);
            }
        }
#endif
    }

    // 如果是工作进程，设置CPU亲和性
    if (worker >= 0)
    {
        cpu_affinity = ngx_get_cpu_affinity(worker);

        if (cpu_affinity)
        {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }

#if (NGX_HAVE_PR_SET_DUMPABLE)

    /* 允许在Linux 2.4.x中setuid()后进行核心转储 */

    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "prctl(PR_SET_DUMPABLE) failed");
    }

#endif

    // 如果设置了工作目录，切换到该目录
    if (ccf->working_directory.len)
    {
        if (chdir((char *)ccf->working_directory.data) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "chdir(\"%s\") failed", ccf->working_directory.data);
            /* 致命错误 */
            exit(2);
        }
    }

    // 清空信号集
    sigemptyset(&set);

    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    // 初始化随机数生成器
    tp = ngx_timeofday();
    srandom(((unsigned)ngx_pid << 16) ^ tp->sec ^ tp->msec);

    // 遍历所有模块，调用init_process回调函数
    for (i = 0; cycle->modules[i]; i++)
    {
        if (cycle->modules[i]->init_process)
        {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR)
            {
                /* 致命错误 */
                exit(2);
            }
        }
    }

    // 关闭其他进程的channel[1]，并关闭当前进程的channel[0]
    for (n = 0; n < ngx_last_process; n++)
    {

        if (ngx_processes[n].pid == -1)
        {
            continue;
        }

        if (n == ngx_process_slot)
        {
            continue;
        }

        if (ngx_processes[n].channel[1] == -1)
        {
            continue;
        }

        if (close(ngx_processes[n].channel[1]) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "close() channel failed");
        }
    }

    if (close(ngx_processes[ngx_process_slot].channel[0]) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() channel failed");
    }

    // 添加channel事件，监听读事件，设置回调函数为ngx_channel_handler
    if (ngx_add_channel_event(cycle, ngx_channel, NGX_READ_EVENT,
                              ngx_channel_handler) == NGX_ERROR)
    {
        /* 致命错误 */
        exit(2);
    }
}

static void
ngx_worker_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t i;
    ngx_connection_t *c;

    for (i = 0; cycle->modules[i]; i++)
    {
        if (cycle->modules[i]->exit_process)
        {
            cycle->modules[i]->exit_process(cycle);
        }
    }

    if (ngx_exiting)
    {
        c = cycle->connections;
        for (i = 0; i < cycle->connection_n; i++)
        {
            if (c[i].fd != -1 && c[i].read && !c[i].read->accept && !c[i].read->channel && !c[i].read->resolver)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                              "*%uA open socket #%d left in connection %ui",
                              c[i].number, c[i].fd, i);
                ngx_debug_quit = 1;
            }
        }

        if (ngx_debug_quit)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "aborting");
            ngx_debug_point();
        }
    }

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */

    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, "exit");

    exit(0);
}

static void
ngx_channel_handler(ngx_event_t *ev)
{
    ngx_int_t n;
    ngx_channel_t ch;
    ngx_connection_t *c;

    if (ev->timedout)
    {
        ev->timedout = 0;
        return;
    }

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel handler");

    for (;;)
    {

        n = ngx_read_channel(c->fd, &ch, sizeof(ngx_channel_t), ev->log);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel: %i", n);

        if (n == NGX_ERROR)
        {

            if (ngx_event_flags & NGX_USE_EPOLL_EVENT)
            {
                ngx_del_conn(c, 0);
            }

            ngx_close_connection(c);
            return;
        }

        if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT)
        {
            if (ngx_add_event(ev, NGX_READ_EVENT, 0) == NGX_ERROR)
            {
                return;
            }
        }

        if (n == NGX_AGAIN)
        {
            return;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "channel command: %ui", ch.command);

        switch (ch.command)
        {

        case NGX_CMD_QUIT:
            ngx_quit = 1;
            break;

        case NGX_CMD_TERMINATE:
            ngx_terminate = 1;
            break;

        case NGX_CMD_REOPEN:
            ngx_reopen = 1;
            break;

        case NGX_CMD_OPEN_CHANNEL:

            ngx_log_debug3(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "get channel s:%i pid:%P fd:%d",
                           ch.slot, ch.pid, ch.fd);

            ngx_processes[ch.slot].pid = ch.pid;
            ngx_processes[ch.slot].channel[0] = ch.fd;
            break;

        case NGX_CMD_CLOSE_CHANNEL:

            ngx_log_debug4(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "close channel s:%i pid:%P our:%P fd:%d",
                           ch.slot, ch.pid, ngx_processes[ch.slot].pid,
                           ngx_processes[ch.slot].channel[0]);

            if (close(ngx_processes[ch.slot].channel[0]) == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                              "close() channel failed");
            }

            ngx_processes[ch.slot].channel[0] = -1;
            break;
        }
    }
}

static void
ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_cache_manager_ctx_t *ctx = data;

    void *ident[4];
    ngx_event_t ev;

    /*
     * Set correct process type since closing listening Unix domain socket
     * in a master process also removes the Unix domain socket file.
     */
    ngx_process = NGX_PROCESS_HELPER;

    ngx_close_listening_sockets(cycle);

    /* Set a moderate number of connections for a helper process. */
    cycle->connection_n = 512;

    ngx_worker_process_init(cycle, -1);

    ngx_memzero(&ev, sizeof(ngx_event_t));
    ev.handler = ctx->handler;
    ev.data = ident;
    ev.log = cycle->log;
    ident[3] = (void *)-1;

    ngx_use_accept_mutex = 0;

    ngx_setproctitle(ctx->name);

    ngx_add_timer(&ev, ctx->delay);

    for (;;)
    {

        if (ngx_terminate || ngx_quit)
        {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_reopen)
        {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }

        ngx_process_events_and_timers(cycle);
    }
}

static void
ngx_cache_manager_process_handler(ngx_event_t *ev)
{
    ngx_uint_t i;
    ngx_msec_t next, n;
    ngx_path_t **path;

    next = 60 * 60 * 1000;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++)
    {

        if (path[i]->manager)
        {
            n = path[i]->manager(path[i]->data);

            next = (n <= next) ? n : next;

            ngx_time_update();
        }
    }

    if (next == 0)
    {
        next = 1;
    }

    ngx_add_timer(ev, next);
}

static void
ngx_cache_loader_process_handler(ngx_event_t *ev)
{
    ngx_uint_t i;
    ngx_path_t **path;
    ngx_cycle_t *cycle;

    cycle = (ngx_cycle_t *)ngx_cycle;

    path = cycle->paths.elts;
    for (i = 0; i < cycle->paths.nelts; i++)
    {

        if (ngx_terminate || ngx_quit)
        {
            break;
        }

        if (path[i]->loader)
        {
            path[i]->loader(path[i]->data);
            ngx_time_update();
        }
    }

    exit(0);
}
