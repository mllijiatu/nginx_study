
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_rbtree_key_t;
typedef ngx_int_t   ngx_rbtree_key_int_t;


typedef struct ngx_rbtree_node_s  ngx_rbtree_node_t;

/*
 * 红黑树节点结构体 ngx_rbtree_node_s 的定义
 */
struct ngx_rbtree_node_s {
    ngx_rbtree_key_t       key;      /* 节点关键字，用于排序 */
    ngx_rbtree_node_t     *left;     /* 指向左子节点的指针 */
    ngx_rbtree_node_t     *right;    /* 指向右子节点的指针 */
    ngx_rbtree_node_t     *parent;   /* 指向父节点的指针 */
    u_char                 color;    /* 节点颜色，红或黑 */
    u_char                 data;     /* 节点数据，可根据需要使用 */
};



typedef struct ngx_rbtree_s  ngx_rbtree_t;

typedef void (*ngx_rbtree_insert_pt) (ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

/*
 * 红黑树结构体 ngx_rbtree_s 的定义
 */
struct ngx_rbtree_s {
    ngx_rbtree_node_t     *root;        /* 指向红黑树的根节点 */
    ngx_rbtree_node_t     *sentinel;    /* 指向红黑树的哨兵节点（即空节点） */
    ngx_rbtree_insert_pt   insert;      /* 红黑树节点插入函数指针 */
};



/**
 * @brief 初始化红黑树
 *
 * @param tree 红黑树结构体指针
 * @param s 红黑树哨兵节点
 * @param i 红黑树的插入函数
 */
#define ngx_rbtree_init(tree, s, i)                                           \
    ngx_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i

/**
 * @brief 获取红黑树节点对应的数据结构体指针
 *
 * @param node 红黑树节点指针
 * @param type 数据结构体类型
 * @param link 数据结构体中红黑树节点的成员
 */
#define ngx_rbtree_data(node, type, link)                                     \
    (type *) ((u_char *) (node) - offsetof(type, link))



void ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_insert_value(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel);
void ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
ngx_rbtree_node_t *ngx_rbtree_next(ngx_rbtree_t *tree,
    ngx_rbtree_node_t *node);


#define ngx_rbt_red(node)               ((node)->color = 1)
#define ngx_rbt_black(node)             ((node)->color = 0)
#define ngx_rbt_is_red(node)            ((node)->color)
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)


/* a sentinel must be black */

#define ngx_rbtree_sentinel_init(node)  ngx_rbt_black(node)


static ngx_inline ngx_rbtree_node_t *
ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _NGX_RBTREE_H_INCLUDED_ */
