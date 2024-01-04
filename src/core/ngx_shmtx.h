
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMTX_H_INCLUDED_
#define _NGX_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * 结构体: ngx_shmtx_sh_t
 * ---------------------
 * 描述: 定义了ngx_shmtx_sh_t结构体，用于存储共享内存中的互斥锁相关信息。
 * 参数:
 *   - ngx_atomic_t lock: 互斥锁本身，用于实现互斥访问。
 *   - ngx_atomic_t wait: 仅在POSIX系统上使用，用于记录等待互斥锁的进程数。
 */
typedef struct {
    ngx_atomic_t   lock;  /* 互斥锁本身，用于实现互斥访问 */
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t   wait;  /* 仅在POSIX系统上使用，用于记录等待互斥锁的进程数 */
#endif
} ngx_shmtx_sh_t;

/*
 * 结构体: ngx_shmtx_t
 * -------------------
 * 描述: 定义了ngx_shmtx_t结构体，用于存储互斥锁的详细信息，包括锁本身、自旋次数等。
 * 参数:
 *   - ngx_atomic_t *lock: 指向共享内存中的互斥锁的指针。
 *   - ngx_atomic_t *wait: 仅在POSIX系统上使用，指向共享内存中的等待计数的指针。
 *   - ngx_uint_t spin: 自旋次数，即在尝试获取互斥锁时，最多自旋等待的次数。
 *   - ngx_fd_t fd: 互斥锁关联的文件描述符，用于实现互斥锁。
 *   - u_char *name: 互斥锁的名称，用于与文件关联。
 *   - ngx_uint_t semaphore: 仅在POSIX系统上使用，记录是否使用信号量。
 *   - sem_t sem: 仅在POSIX系统上使用，信号量结构体。
 */
typedef struct {
#if (NGX_HAVE_ATOMIC_OPS)
    ngx_atomic_t  *lock;       /* 指向共享内存中的互斥锁的指针 */
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t  *wait;       /* 仅在POSIX系统上使用，指向共享内存中的等待计数的指针 */
    ngx_uint_t     semaphore;  /* 仅在POSIX系统上使用，记录是否使用信号量 */
    sem_t          sem;        /* 仅在POSIX系统上使用，信号量结构体 */
#endif
#else
    ngx_fd_t       fd;         /* 互斥锁关联的文件描述符，用于实现互斥锁 */
    u_char        *name;       /* 互斥锁的名称，用于与文件关联 */
#endif
    ngx_uint_t     spin;        /* 自旋次数，即在尝试获取互斥锁时，最多自旋等待的次数 */
} ngx_shmtx_t;



ngx_int_t ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr,
    u_char *name);
void ngx_shmtx_destroy(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_trylock(ngx_shmtx_t *mtx);
void ngx_shmtx_lock(ngx_shmtx_t *mtx);
void ngx_shmtx_unlock(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid);


#endif /* _NGX_SHMTX_H_INCLUDED_ */
