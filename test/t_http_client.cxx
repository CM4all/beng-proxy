#define HAVE_EXPECT_100
#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_CLOSE_IGNORED_REQUEST_BODY
#define ENABLE_HUGE_BODY
#define USE_BUCKETS

#include "t_client.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "system/fd-util.h"
#include "system/fd_util.h"
#include "direct.hxx"
#include "fb_pool.hxx"
#include "RootPool.hxx"
#include "event/Event.hxx"

#include <sys/wait.h>

struct Connection {
    const pid_t pid;
    const int fd;

    Connection(pid_t _pid, int _fd):pid(_pid), fd(_fd) {}
    static Connection *New(const char *path, const char *mode);

    ~Connection();

    void Request(struct pool *pool,
                 Lease &lease,
                 http_method_t method, const char *uri,
                 struct strmap *headers,
                 Istream *body,
                 bool expect_100,
                 const struct http_response_handler *handler,
                 void *ctx,
                 struct async_operation_ref *async_ref) {
        http_client_request(*pool, fd, FdType::FD_SOCKET,
                            lease,
                            "localhost",
                            nullptr, nullptr,
                            method, uri, HttpHeaders(headers), body, expect_100,
                            *handler, ctx, *async_ref);
    }

    static Connection *NewMirror(struct pool &) {
        return New("./test/run_http_server", "mirror");
    }

    static Connection *NewNull(struct pool &) {
        return New("./test/run_http_server", "null");
    }

    static Connection *NewDummy(struct pool &) {
        return New("./test/run_http_server", "dummy");
    }

    static Connection *NewFixed(struct pool &) {
        return New("./test/run_http_server", "fixed");
    }

    static Connection *NewTiny(struct pool &p) {
        return NewFixed(p);
    }

    static Connection *NewHuge(struct pool &) {
        return New("./test/run_http_server", "huge");
    }

    static Connection *NewTwice100(struct pool &) {
        return New("./test/twice_100.sh", nullptr);
    }

    static Connection *NewClose100(struct pool &);

    static Connection *NewHold(struct pool &) {
        return New("./test/run_http_server", "hold");
    }
};

Connection::~Connection()
{
    assert(pid >= 1);
    assert(fd >= 0);

    close(fd);

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid() failed");
        exit(EXIT_FAILURE);
    }

    assert(!WIFSIGNALED(status));
}

Connection *
Connection::New(const char *path, const char *mode)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv);
    if (ret < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        dup2(sv[1], 0);
        close(sv[0]);
        close(sv[1]);
        execl(path, path,
              "0", "0", mode, nullptr);

        const char *srcdir = getenv("srcdir");
        if (srcdir != nullptr) {
            /* support automake out-of-tree build */
            if (chdir(srcdir) == 0)
                execl(path, path,
                      "0", "0", mode, nullptr);
        }

        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    fd_set_nonblock(sv[0], 1);

    return new Connection(pid, sv[0]);
}

Connection *
Connection::NewClose100(struct pool &)
{
    int sv[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        close(sv[0]);

        static const char response[] = "HTTP/1.1 100 Continue\n\n";
        (void)write(sv[1], response, sizeof(response) - 1);
        shutdown(sv[1], SHUT_WR);

        char buffer[64];
        while (read(sv[1], buffer, sizeof(buffer)) > 0) {}

        exit(EXIT_SUCCESS);
    }

    close(sv[1]);

    fd_set_nonblock(sv[0], 1);

    return new Connection(pid, sv[0]);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    EventBase event_base;
    fb_pool_init(false);

    run_all_tests<Connection>(RootPool());

    fb_pool_deinit();
}
