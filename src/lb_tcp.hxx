/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

#include "FdType.hxx"

typedef struct _GError GError;
struct pool;
struct Stock;
struct SocketFilter;
struct AddressList;
struct balancer;
struct lb_tcp;
class SocketDescriptor;
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
lb_tcp_new(struct pool *pool, Stock *pipe_stock,
           SocketDescriptor &&fd, FdType fd_type,
           const SocketFilter *filter, void *filter_ctx,
           SocketAddress remote_address,
           bool transparent_source,
           const AddressList &address_list,
           struct balancer &balancer,
           const struct lb_tcp_handler *handler, void *ctx,
           lb_tcp **tcp_r);

void
lb_tcp_close(struct lb_tcp *tcp);

#endif
