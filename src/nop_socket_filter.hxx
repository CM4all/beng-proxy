/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOP_SOCKET_FILTER_HXX
#define BENG_PROXY_NOP_SOCKET_FILTER_HXX

struct pool;

/**
 * A module for #filtered_socket that does not filter anything.  It
 * passes data as-is.  It is meant for debugging.
 */
extern const struct socket_filter nop_socket_filter;

void *
nop_socket_filter_new(struct pool &pool);

#endif
