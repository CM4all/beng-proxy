#include "session.hxx"
#include "session_manager.hxx"
#include "cookie_jar.hxx"
#include "shm/dpool.hxx"
#include "crash.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

TEST(SessionTest, Basic)
{
    EventLoop event_loop;

    crash_global_init();
    session_manager_init(event_loop, std::chrono::minutes(30), 0, 0);
    session_manager_event_del();

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
        session_manager_event_add();

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

    session_manager_deinit();
    crash_global_deinit();
}
