/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOP_THREAD_SOCKET_FILTER_H
#define BENG_PROXY_NOP_THREAD_SOCKET_FILTER_H

struct pool;

/**
 * A module for #thread_socket_filter does not filter anything.  It
 * passes data as-is.  It is meant for debugging.
 */
extern const struct ThreadSocketFilterHandler nop_thread_socket_filter;

void *
nop_thread_socket_filter_new(struct pool *pool);

#endif
