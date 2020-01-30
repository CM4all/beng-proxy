/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A queue that manages work for worker threads.
 */

#pragma once

class EventLoop;
class ThreadQueue;
class ThreadJob;

ThreadQueue *
thread_queue_new(EventLoop &event_loop) noexcept;

/**
 * Cancel all thread_queue_wait() calls and refuse all further calls.
 * This is used to initiate shutdown of all threads connected to this
 * queue.
 */
void
thread_queue_stop(ThreadQueue &q) noexcept;

void
thread_queue_free(ThreadQueue *q) noexcept;

/**
 * Enqueue a job, and wake up an idle thread (if there is any).
 */
void
thread_queue_add(ThreadQueue &q, ThreadJob &job) noexcept;

/**
 * Dequeue an existing job or wait for a new job, and reserve it.
 *
 * @return NULL if thread_queue_stop() has been called
 */
ThreadJob *
thread_queue_wait(ThreadQueue &q) noexcept;

/**
 * Mark the specified job (returned by thread_queue_wait()) as "done".
 */
void
thread_queue_done(ThreadQueue &q, ThreadJob &job) noexcept;

/**
 * Cancel a job that has been queued.
 *
 * @return true if the job is now canceled, false if the job is
 * currently being processed
 */
bool
thread_queue_cancel(ThreadQueue &q, ThreadJob &job) noexcept;
