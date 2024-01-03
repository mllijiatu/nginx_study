
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


/*
 * 该头文件定义了NGINX的版本信息和相关宏定义，以及一些预处理指令。
 * 在整个NGINX源代码中，这个头文件的作用是确保在编译时只包含一次。
 */

#ifndef _NGINX_H_INCLUDED_
#define _NGINX_H_INCLUDED_

/*
 * 定义NGINX的主版本号、次版本号和修订号。
 * 例如，版本号1.24.0被定义为1024000。
 */
#define nginx_version      1024000
#define NGINX_VERSION      "1.24.0"
#define NGINX_VER          "nginx/" NGINX_VERSION

/*
 * 如果在编译时定义了NGX_BUILD，则NGINX_VER_BUILD包含构建信息。
 * 否则，只包含基本版本信息。
 */
#ifdef NGX_BUILD
#define NGINX_VER_BUILD    NGINX_VER " (" NGX_BUILD ")"
#else
#define NGINX_VER_BUILD    NGINX_VER
#endif

/*
 * 定义NGINX的环境变量名和用于旧进程的后缀。
 */
#define NGINX_VAR          "NGINX"
#define NGX_OLDPID_EXT     ".oldbin"

/*
 * 结束NGINX头文件的宏定义防卫。
 */
#endif /* _NGINX_H_INCLUDED_ */

