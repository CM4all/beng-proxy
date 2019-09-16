/*
 * Copyright 2007-2018 Content Management AG
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

#include "event/TimerEvent.hxx"
#include "util/Compiler.h"

#include <chrono>
#include <utility>

struct Session;
struct SessionContainer;
class SessionId;
class EventLoop;

class SessionManager {
    /** clean up expired sessions every 60 seconds */
    static constexpr Event::Duration cleanup_interval = std::chrono::minutes(1);

    const unsigned cluster_size, cluster_node;

    struct shm *shm;

    SessionContainer *container;

    TimerEvent cleanup_timer;

public:
    SessionManager(EventLoop &event_loop, std::chrono::seconds idle_timeout,
                   unsigned _cluster_size, unsigned _cluster_node) noexcept;

    ~SessionManager() noexcept;

    /**
     * Re-add all libevent events after DisableEvents().
     */
    void EnableEvents() {
        cleanup_timer.Schedule(cleanup_interval);
    }

    /**
     * Removes all libevent events.  Call this before fork(), or
     * before creating a new event base.  Don't forget to call
     * EnableEvents() afterwards.
     */
    void DisableEvents() {
        cleanup_timer.Cancel();
    }

    void Ref() noexcept;

    void Abandon() noexcept;

    gcc_pure
    bool IsAbandoned() const noexcept;

    void AdjustNewSessionId(SessionId &id) noexcept;

    /**
     * Returns the number of sessions.
     */
    gcc_pure
    unsigned LockCount() noexcept;

    /**
     * Invoke the callback for each session.  The session and the
     * session manager will be locked during the callback.
     */
    bool Visit(bool (*callback)(const Session *session,
                                void *ctx), void *ctx);

    gcc_pure
    Session *LockFind(SessionId id) noexcept;

    /**
     * Add an initialized #Session object to the session manager.  Its
     * #dpool will be destroyed automatically when the session
     * expires.  After returning from this function, the session is
     * protected and the pointer must not be used, unless it is looked
     * up (and thus locked).
     */
    void Insert(Session &session) noexcept;
    void EraseAndDispose(SessionId id) noexcept;
    void ReplaceAndDispose(Session &old_session,
                           Session &new_session) noexcept;

    void Defragment(SessionId id) noexcept;

    bool Purge() noexcept;

    void Cleanup() noexcept;

    /**
     * Create a new #dpool object.  The caller is responsible for
     * destroying it or adding a new session with this #dpool, see
     * Insert().
     */
    struct dpool *NewDpool() noexcept;

    struct dpool *NewDpoolHarder() {
        auto *pool = NewDpool();
        if (pool == nullptr && Purge())
            /* at least one session has been purged: try again */
            pool = NewDpool();

        return pool;
    }
};

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

#endif
