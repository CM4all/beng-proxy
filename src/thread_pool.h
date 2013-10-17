/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_POOL_H
#define BENG_PROXY_THREAD_POOL_H

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the global #thread_queue instance.  The first call to this
 * function creates the queue and starts the worker threads.  To shut
 * down, call thread_pool_stop(), thread_pool_join() and
 * thread_pool_deinit().
 *
 * @param pool a global pool that will be destructed after the
 * thread_pool_deinit() call
 */
struct thread_queue *
thread_pool_get_queue(struct pool *pool);

void
thread_pool_stop(void);

void
thread_pool_join(void);

void
thread_pool_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
