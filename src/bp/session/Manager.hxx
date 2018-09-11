/*
 * Copyright 2007-2017 Content Management AG
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
 * Session management.
 */

#ifndef BENG_PROXY_SESSION_MANAGER_HXX
#define BENG_PROXY_SESSION_MANAGER_HXX

#include "util/Compiler.h"

#include <chrono>

struct Session;
class EventLoop;

/**
 * Initialize the global session manager or increase the reference
 * counter.
 *
 * @param idle_timeout the idle timeout of sessions
 * @param cluster_size the number of nodes in the cluster
 * @param cluster_node the index of this node in the cluster
 */
void
session_manager_init(EventLoop &event_loop, std::chrono::seconds idle_timeout,
                     unsigned cluster_size, unsigned cluster_node);

/**
 * Decrease the reference counter and destroy the global session
 * manager if it has become zero.
 */
void
session_manager_deinit();

/**
 * Release the session manager and try not to access the shared
 * memory, because we assume it may be corrupted.
 */
void
session_manager_abandon();

/**
 * Re-add all libevent events after session_manager_event_del().
 */
void
session_manager_event_add();

/**
 * Removes all libevent events.  Call this before fork(), or before
 * creating a new event base.  Don't forget to call
 * session_manager_event_add() afterwards.
 */
void
session_manager_event_del();

/**
 * Returns the number of sessions.
 */
gcc_pure
unsigned
session_manager_get_count();

/**
 * Create a new #dpool object.  The caller is responsible for
 * destroying it or adding a new session with this #dpool, see
 * session_manager_add().
 */
struct dpool *
session_manager_new_dpool();

/**
 * Add an initialized #session object to the session manager.  Its
 * #dpool will be destroyed automatically when the session expires.
 * After returning from this function, the session is protected and
 * the pointer must not be used, unless it is looked up (and thus
 * locked).
 */
void
session_manager_add(Session &session);

/**
 * Create a new session with a random session id.
 *
 * The returned session object is locked and must be unlocked with
 * session_put().
 */
Session * gcc_malloc
session_new();

/**
 * Invoke the callback for each session.  The session and the session
 * manager will be locked during the callback.
 */
bool
session_manager_visit(bool (*callback)(const Session *session,
                                       void *ctx), void *ctx);

#endif
