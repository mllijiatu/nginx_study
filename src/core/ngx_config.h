
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONFIG_H_INCLUDED_
#define _NGX_CONFIG_H_INCLUDED_


#include <ngx_auto_headers.h>


#if defined __DragonFly__ && !defined __FreeBSD__
#define __FreeBSD__        4
#define __FreeBSD_version  480101
#endif


#if (NGX_FREEBSD)
#include <ngx_freebsd_config.h>


#elif (NGX_LINUX)
#include <ngx_linux_config.h>


#elif (NGX_SOLARIS)
#include <ngx_solaris_config.h>


#elif (NGX_DARWIN)
#include <ngx_darwin_config.h>


#elif (NGX_WIN32)
#include <ngx_win32_config.h>


#else /* POSIX */
#include <ngx_posix_config.h>

#endif


#ifndef NGX_HAVE_SO_SNDLOWAT
#define NGX_HAVE_SO_SNDLOWAT     1
#endif


#if !(NGX_WIN32)

/*
 * ngx_signal_helper - 信号宏辅助函数，用于将信号值转换为SIG开头的宏
 *
 * 该宏辅助函数用于将信号值转换为对应的SIG开头的宏，例如 ngx_signal_helper(QUIT) 会被展开为 SIGQUIT。
 *
 * 参数:
 *     n - 信号值
 */
#define ngx_signal_helper(n)     SIG##n

/*
 * ngx_signal_value - 信号值宏，用于将信号值转换为SIG开头的宏
 *
 * 该宏用于将信号值转换为对应的SIG开头的宏，通过调用 ngx_signal_helper 辅助函数实现。
 *
 * 参数:
 *     n - 信号值
 */
#define ngx_signal_value(n)      ngx_signal_helper(n)


#define ngx_random               random

/* TODO: #ifndef */
#define NGX_SHUTDOWN_SIGNAL      QUIT
#define NGX_TERMINATE_SIGNAL     TERM
#define NGX_NOACCEPT_SIGNAL      WINCH
#define NGX_RECONFIGURE_SIGNAL   HUP

#if (NGX_LINUXTHREADS)
#define NGX_REOPEN_SIGNAL        INFO
#define NGX_CHANGEBIN_SIGNAL     XCPU
#else
#define NGX_REOPEN_SIGNAL        USR1
#define NGX_CHANGEBIN_SIGNAL     USR2
#endif

#define ngx_cdecl
#define ngx_libc_cdecl

#endif

/*
 * 类型定义: ngx_fd_t
 * ----------------
 * 描述: 定义了ngx_fd_t类型，表示文件描述符的数据类型。
 */
typedef int                      ngx_fd_t;

/*
 * 结构体定义: ngx_file_info_t
 * --------------------------
 * 描述: 定义了ngx_file_info_t结构体，用于存储文件信息，具体内容可能依赖于平台。
 */
typedef struct stat              ngx_file_info_t;

/*
 * 类型定义: ngx_file_uniq_t
 * ------------------------
 * 描述: 定义了ngx_file_uniq_t类型，表示文件的唯一标识符的数据类型。
 */
typedef ino_t                    ngx_file_uniq_t;



#define NGX_INT32_LEN   (sizeof("-2147483648") - 1)
#define NGX_INT64_LEN   (sizeof("-9223372036854775808") - 1)

#if (NGX_PTR_SIZE == 4)
#define NGX_INT_T_LEN   NGX_INT32_LEN
#define NGX_MAX_INT_T_VALUE  2147483647

#else
#define NGX_INT_T_LEN   NGX_INT64_LEN
#define NGX_MAX_INT_T_VALUE  9223372036854775807
#endif


#ifndef NGX_ALIGNMENT
#define NGX_ALIGNMENT   sizeof(unsigned long)    /* platform word */
#endif

/**
 * 宏定义 ngx_align 用于将指定大小的数据按指定对齐字节数进行对齐。
 * @param d 要对齐的数据
 * @param a 对齐字节数
 * @return 返回对齐后的结果
 */
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))

/**
 * 宏定义 ngx_align_ptr 用于将指针按指定对齐字节数进行对齐。
 * @param p 要对齐的指针
 * @param a 对齐字节数
 * @return 返回对齐后的指针
 */
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))



#define ngx_abort       abort


/* TODO: platform specific: array[NGX_INVALID_ARRAY_INDEX] must cause SIGSEGV */
#define NGX_INVALID_ARRAY_INDEX 0x80000000


/* TODO: auto_conf: ngx_inline   inline __inline __inline__ */
#ifndef ngx_inline
#define ngx_inline      inline
#endif

#ifndef INADDR_NONE  /* Solaris */
#define INADDR_NONE  ((unsigned int) -1)
#endif

#ifdef MAXHOSTNAMELEN
#define NGX_MAXHOSTNAMELEN  MAXHOSTNAMELEN
#else
#define NGX_MAXHOSTNAMELEN  256
#endif


#define NGX_MAX_UINT32_VALUE  (uint32_t) 0xffffffff
#define NGX_MAX_INT32_VALUE   (uint32_t) 0x7fffffff


#if (NGX_COMPAT)

#define NGX_COMPAT_BEGIN(slots)  uint64_t spare[slots];
#define NGX_COMPAT_END

#else

#define NGX_COMPAT_BEGIN(slots)
#define NGX_COMPAT_END

#endif


#endif /* _NGX_CONFIG_H_INCLUDED_ */
