#include "session.hxx"
#include "session_manager.hxx"
#include "cookie_jar.hxx"
#include "crash.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <event.h>

struct cookie_jar *
cookie_jar_new(struct dpool &pool gcc_unused)
{
    return NULL;
}

struct cookie_jar *
cookie_jar::Dup(struct dpool &new_pool gcc_unused) const
{
    return NULL;
}

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct event_base *event_base = event_init();

    crash_global_init();
    session_manager_init(1200, 0, 0);
    session_manager_event_del();

    int fds[2];
    (void)pipe(fds);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        event_base_free(event_base);
        event_base = event_init();
        session_manager_init(1200, 0, 0);

        auto *session = session_new();
        (void)write(fds[1], &session->id, sizeof(session->id));
        session_put(session);
    } else {
        session_manager_event_add();

        close(fds[1]);

        int status;
        pid_t pid2 = wait(&status);
        assert(pid2 == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 0);

        SessionId session_id;
        (void)read(fds[0], &session_id, sizeof(session_id));

        auto *session = session_get(session_id);
        assert(session != NULL);
        assert(session_id_equals(session->id, session_id));
        session_put(session);
    }

    session_manager_deinit();
    crash_global_deinit();

    event_base_free(event_base);
}
