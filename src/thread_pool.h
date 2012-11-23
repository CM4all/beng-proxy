/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_POOL_H
#define BENG_PROXY_THREAD_POOL_H

struct pool;

extern struct thread_queue *global_thread_queue;

void
thread_pool_init(struct pool *pool);

void
thread_pool_start(void);

void
thread_pool_stop(void);

void
thread_pool_join(void);

void
thread_pool_deinit(void);

#endif
