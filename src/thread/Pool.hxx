// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class EventLoop;

/**
 * A queue that manages work for worker threads.
 */
class ThreadQueue;

/**
 * Returns the global #ThreadQueue instance.  The first call to this
 * function creates the queue and starts the worker threads.  To shut
 * down, call thread_pool_stop(), thread_pool_join() and
 * thread_pool_deinit().
 *
 * @param pool a global pool that will be destructed after the
 * thread_pool_deinit() call
 */
[[gnu::const]]
ThreadQueue &
thread_pool_get_queue(EventLoop &event_loop) noexcept;

void
thread_pool_set_volatile() noexcept;

void
thread_pool_stop() noexcept;

void
thread_pool_join() noexcept;

void
thread_pool_deinit() noexcept;
