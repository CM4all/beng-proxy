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

#include "Glue.hxx"
#include "Manager.hxx"
#include "Session.hxx"
#include "random.hxx"
#include "crash.hxx"

SessionManager *session_manager;

void
session_manager_init(EventLoop &event_loop, std::chrono::seconds idle_timeout,
                     unsigned cluster_size, unsigned cluster_node)
{
    assert((cluster_size == 0 && cluster_node == 0) ||
           cluster_node < cluster_size);

    random_seed();

    if (session_manager == nullptr) {
        session_manager = new SessionManager(event_loop, idle_timeout,
                                             cluster_size, cluster_node);
    } else {
        session_manager->Ref();
    }
}

void
session_manager_deinit() noexcept
{
    assert(session_manager != nullptr);

    delete session_manager;
    session_manager = nullptr;
}

void
session_manager_abandon() noexcept
{
    assert(session_manager != nullptr);

    session_manager->Abandon();
    delete session_manager;
    session_manager = nullptr;
}

Session *
session_new() noexcept
{
    crash_unsafe_enter();
    Session *session = session_manager->CreateSession();
    if (session == nullptr)
        crash_unsafe_leave();
    return session;
}

Session *
session_get(SessionId id) noexcept
{
    if (!id.IsDefined())
        return nullptr;

    crash_unsafe_enter();

    Session *session = session_manager->LockFind(id);

    if (session == nullptr)
        crash_unsafe_leave();

    return session;
}

void
session_put(Session *session) noexcept
{
    session_manager->Put(*session);
    crash_unsafe_leave();
}

void
session_delete(SessionId id) noexcept
{
    session_manager->EraseAndDispose(id);
}
