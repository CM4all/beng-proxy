/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

#include "istream-direct.h"

typedef struct _GError GError;
struct pool;
struct stock;
struct socket_filter;
struct address_list;
struct balancer;
struct lb_tcp;
class SocketAddress;

struct lb_tcp_handler {
    void (*eof)(void *ctx);
    void (*error)(const char *prefix, const char *error, void *ctx);
    void (*_errno)(const char *prefix, int error, void *ctx);
    void (*gerror)(const char *prefix, GError *error, void *ctx);
};

/**
 * @param transparent_source see #lb_cluster_config::transparent_source
 */
void
lb_tcp_new(struct pool *pool, struct stock *pipe_stock,
           int fd, enum istream_direct fd_type,
           const struct socket_filter *filter, void *filter_ctx,
           SocketAddress remote_address,
           bool transparent_source,
           const struct address_list &address_list,
           struct balancer &balancer,
           const struct lb_tcp_handler *handler, void *ctx,
           lb_tcp **tcp_r);

void
lb_tcp_close(struct lb_tcp *tcp);

#endif
