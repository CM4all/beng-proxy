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

#include "bp/session/Session.hxx"
#include "bp/session/Glue.hxx"
#include "shm/dpool.hxx"
#include "crash.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

TEST(SessionTest, Basic)
{
    const ScopeCrashGlobalInit crash_init;
    EventLoop event_loop;

    const ScopeSessionManagerInit sm_init(event_loop, std::chrono::minutes(30),
                                          0, 0);

    int fds[2];
    (void)pipe(fds);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        event_loop.Reinit();
        session_manager_init(event_loop, std::chrono::minutes(30), 0, 0);

        auto *session = session_new();
        (void)write(fds[1], &session->id, sizeof(session->id));
        session_put(session);
    } else {
        close(fds[1]);

        int status;
        pid_t pid2 = wait(&status);
        ASSERT_EQ(pid2, pid);
        ASSERT_TRUE(WIFEXITED(status));
        ASSERT_EQ(WEXITSTATUS(status), 0);

        SessionId session_id;
        (void)read(fds[0], &session_id, sizeof(session_id));

        SessionLease session(session_id);
        ASSERT_TRUE(session);
        ASSERT_EQ(session->id, session_id);
    }
}
