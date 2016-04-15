/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOP_THREAD_SOCKET_FILTER_H
#define BENG_PROXY_NOP_THREAD_SOCKET_FILTER_H

struct pool;
class ThreadSocketFilterHandler;

ThreadSocketFilterHandler *
nop_thread_socket_filter_new(struct pool *pool);

#endif
