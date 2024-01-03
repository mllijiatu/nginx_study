
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ngx_queue_t  ngx_posted_accept_events;
ngx_queue_t  ngx_posted_next_events;
ngx_queue_t  ngx_posted_events;


/*
 * 处理已经添加到posted队列中的事件。
 *
 * 参数:
 *   cycle: 指向ngx_cycle_t结构的指针，表示当前的循环。
 *   posted: 指向ngx_queue_t结构的指针，表示包含已经添加到posted队列的事件的队列。
 */
void ngx_event_process_posted(ngx_cycle_t *cycle, ngx_queue_t *posted)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    // 遍历posted队列中的所有事件，直到队列为空
    while (!ngx_queue_empty(posted)) {

        // 获取posted队列中的头节点
        q = ngx_queue_head(posted);

        // 从头节点中获取事件
        ev = ngx_queue_data(q, ngx_event_t, queue);

        // 记录调试信息，表示处理了一个已经添加到posted队列的事件
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted event %p", ev);

        // 从posted队列中删除当前事件
        ngx_delete_posted_event(ev);

        // 调用事件的处理函数
        ev->handler(ev);
    }
}



void
ngx_event_move_posted_next(ngx_cycle_t *cycle)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    for (q = ngx_queue_head(&ngx_posted_next_events);
         q != ngx_queue_sentinel(&ngx_posted_next_events);
         q = ngx_queue_next(q))
    {
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted next event %p", ev);

        ev->ready = 1;
        ev->available = -1;
    }

    ngx_queue_add(&ngx_posted_events, &ngx_posted_next_events);
    ngx_queue_init(&ngx_posted_next_events);
}
