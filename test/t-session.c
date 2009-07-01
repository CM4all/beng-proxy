#include "session.h"
#include "cookie-client.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <event.h>

struct cookie_jar *
cookie_jar_new(struct dpool *pool __attr_unused)
{
    return NULL;
}

struct cookie_jar *
cookie_jar_dup(struct dpool *pool __attr_unused,
               const struct cookie_jar *src __attr_unused)
{
    return NULL;
}

int main(int argc __attr_unused, char **argv __attr_unused) {
    struct event_base *event_base;
    pid_t pid;
    int fds[2];

    event_base = event_init();

    session_manager_init();
    session_manager_event_del();

    pipe(fds);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        struct session *session;

        event_base_free(event_base);
        event_base = event_init();
        session_manager_init();

        session = session_new();
        write(fds[1], &session->id, sizeof(session->id));
        session_put(session);
    } else {
        pid_t pid2;
        int status;
        session_id_t session_id;
        struct session *session;

        session_manager_event_add();

        close(fds[1]);

        pid2 = wait(&status);
        assert(pid2 == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 0);

        read(fds[0], &session_id, sizeof(session_id));

        session = session_get(session_id);
        assert(session != NULL);
        assert(session->id == session_id);
        session_put(session);
    }

    session_manager_deinit();

    event_base_free(event_base);
}
