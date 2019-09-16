/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

#include "util/Compiler.h"

#include <chrono>
#include <utility>

struct Session;
class SessionId;
class EventLoop;

class SessionManager;

/** the one and only session manager instance */
extern SessionManager *session_manager;

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
 * Create a new session with a random session id.
 *
 * The returned session object is locked and must be unlocked with
 * session_put().
 */
Session * gcc_malloc
session_new();

class ScopeSessionManagerInit {
public:
    template<typename... Args>
    ScopeSessionManagerInit(Args&&... args) {
        session_manager_init(std::forward<Args>(args)...);
    }

    ~ScopeSessionManagerInit() noexcept {
        session_manager_deinit();
    }

    ScopeSessionManagerInit(const ScopeSessionManagerInit &) = delete;
    ScopeSessionManagerInit &operator=(const ScopeSessionManagerInit &) = delete;
};
