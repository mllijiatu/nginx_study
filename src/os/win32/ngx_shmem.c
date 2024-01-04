
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * Base addresses selected by system for shared memory mappings are likely
 * to be different on Windows Vista and later versions due to address space
 * layout randomization.  This is however incompatible with storing absolute
 * addresses within the shared memory.
 *
 * To make it possible to store absolute addresses we create mappings
 * at the same address in all processes by starting mappings at predefined
 * addresses.  The addresses were selected somewhat randomly in order to
 * minimize the probability that some other library doing something similar
 * conflicts with us.  The addresses are from the following typically free
 * blocks:
 *
 * - 0x10000000 .. 0x70000000 (about 1.5 GB in total) on 32-bit platforms
 * - 0x000000007fff0000 .. 0x000007f68e8b0000 (about 8 TB) on 64-bit platforms
 *
 * Additionally, we allow to change the mapping address once it was detected
 * to be different from one originally used.  This is needed to support
 * reconfiguration.
 */


#ifdef _WIN64
#define NGX_SHMEM_BASE  0x0000047047e00000
#else
#define NGX_SHMEM_BASE  0x2efe0000
#endif


ngx_uint_t  ngx_allocation_granularity;


/*
 * 函数: ngx_int_t ngx_shm_alloc
 * ---------------------------
 * 描述: 在共享内存中分配空间，创建文件映射，并映射到当前进程的地址空间。
 * 参数: 
 *   - ngx_shm_t *shm: ngx_shm_t结构体指针，包含了共享内存的相关信息。
 * 返回: NGX_OK 表示成功，NGX_ERROR 表示失败。
 */
ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    u_char         *name;
    uint64_t        size;
    static u_char  *base = (u_char *) NGX_SHMEM_BASE;

    /* 分配用于存储文件映射名称的内存空间 */
    name = ngx_alloc(shm->name.len + 2 + NGX_INT32_LEN, shm->log);
    if (name == NULL) {
        return NGX_ERROR;
    }

    /* 构建文件映射的名称 */
    (void) ngx_sprintf(name, "%V_%s%Z", &shm->name, ngx_unique);

    ngx_set_errno(0);

    size = shm->size;

    /* 创建文件映射对象 */
    shm->handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                    (u_long) (size >> 32),
                                    (u_long) (size & 0xffffffff),
                                    (char *) name);

    if (shm->handle == NULL) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "CreateFileMapping(%uz, %s) failed",
                      shm->size, name);
        ngx_free(name);

        return NGX_ERROR;
    }

    ngx_free(name);

    /* 检查是否已经存在同名的文件映射对象 */
    if (ngx_errno == ERROR_ALREADY_EXISTS) {
        shm->exists = 1;
    }

    /* 映射文件到当前进程的地址空间 */
    shm->addr = MapViewOfFileEx(shm->handle, FILE_MAP_WRITE, 0, 0, 0, base);

    if (shm->addr != NULL) {
        base += ngx_align(size, ngx_allocation_granularity);
        return NGX_OK;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_CORE, shm->log, ngx_errno,
                   "MapViewOfFileEx(%uz, %p) of file mapping \"%V\" failed, "
                   "retry without a base address",
                   shm->size, base, &shm->name);

    /*
     * 文件映射的顺序可能在主进程和工作进程重新配置后不同。
     * 因此，以上可能会由于与先前创建的映射重新映射到不同地址的冲突而失败。
     * 此外，内存的其他使用可能会导致失败。在这种情况下，我们重新尝试没有基地址，
     * 让系统自己分配地址。
     */

    /* 重新映射文件到当前进程的地址空间 */
    shm->addr = MapViewOfFile(shm->handle, FILE_MAP_WRITE, 0, 0, 0);

    if (shm->addr != NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                  "MapViewOfFile(%uz) of file mapping \"%V\" failed",
                  shm->size, &shm->name);

    /* 映射失败，关闭文件映射对象 */
    if (CloseHandle(shm->handle) == 0) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "CloseHandle() of file mapping \"%V\" failed",
                      &shm->name);
    }

    return NGX_ERROR;
}

{
    u_char         *name;
    uint64_t        size;
    static u_char  *base = (u_char *) NGX_SHMEM_BASE;

    name = ngx_alloc(shm->name.len + 2 + NGX_INT32_LEN, shm->log);
    if (name == NULL) {
        return NGX_ERROR;
    }

    (void) ngx_sprintf(name, "%V_%s%Z", &shm->name, ngx_unique);

    ngx_set_errno(0);

    size = shm->size;

    shm->handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                    (u_long) (size >> 32),
                                    (u_long) (size & 0xffffffff),
                                    (char *) name);

    if (shm->handle == NULL) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "CreateFileMapping(%uz, %s) failed",
                      shm->size, name);
        ngx_free(name);

        return NGX_ERROR;
    }

    ngx_free(name);

    if (ngx_errno == ERROR_ALREADY_EXISTS) {
        shm->exists = 1;
    }

    shm->addr = MapViewOfFileEx(shm->handle, FILE_MAP_WRITE, 0, 0, 0, base);

    if (shm->addr != NULL) {
        base += ngx_align(size, ngx_allocation_granularity);
        return NGX_OK;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_CORE, shm->log, ngx_errno,
                   "MapViewOfFileEx(%uz, %p) of file mapping \"%V\" failed, "
                   "retry without a base address",
                   shm->size, base, &shm->name);

    /*
     * Order of shared memory zones may be different in the master process
     * and worker processes after reconfiguration.  As a result, the above
     * may fail due to a conflict with a previously created mapping remapped
     * to a different address.  Additionally, there may be a conflict with
     * some other uses of the memory.  In this case we retry without a base
     * address to let the system assign the address itself.
     */

    shm->addr = MapViewOfFile(shm->handle, FILE_MAP_WRITE, 0, 0, 0);

    if (shm->addr != NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                  "MapViewOfFile(%uz) of file mapping \"%V\" failed",
                  shm->size, &shm->name);

    if (CloseHandle(shm->handle) == 0) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "CloseHandle() of file mapping \"%V\" failed",
                      &shm->name);
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_shm_remap(ngx_shm_t *shm, u_char *addr)
{
    if (UnmapViewOfFile(shm->addr) == 0) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "UnmapViewOfFile(%p) of file mapping \"%V\" failed",
                      shm->addr, &shm->name);
        return NGX_ERROR;
    }

    shm->addr = MapViewOfFileEx(shm->handle, FILE_MAP_WRITE, 0, 0, 0, addr);

    if (shm->addr != NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                  "MapViewOfFileEx(%uz, %p) of file mapping \"%V\" failed",
                  shm->size, addr, &shm->name);

    return NGX_ERROR;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (UnmapViewOfFile(shm->addr) == 0) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "UnmapViewOfFile(%p) of file mapping \"%V\" failed",
                      shm->addr, &shm->name);
    }

    if (CloseHandle(shm->handle) == 0) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "CloseHandle() of file mapping \"%V\" failed",
                      &shm->name);
    }
}
