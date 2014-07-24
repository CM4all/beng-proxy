#define HAVE_EXPECT_100
#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_CLOSE_IGNORED_REQUEST_BODY

#include "t_client.hxx"
#include "http_client.hxx"
#include "header_writer.hxx"
#include "growing_buffer.hxx"
#include "fd-util.h"
#include "direct.h"
#include "fd_util.h"
#include "fb_pool.hxx"

#include <sys/wait.h>

struct connection {
    pid_t pid;
    int fd;
};

static void
client_request(struct pool *pool, struct connection *connection,
               const struct lease *lease, void *lease_ctx,
               http_method_t method, const char *uri,
               struct strmap *headers,
               struct istream *body,
               bool expect_100,
               const struct http_response_handler *handler,
               void *ctx,
               struct async_operation_ref *async_ref)
{
    struct growing_buffer *headers2 = nullptr;
    if (headers != nullptr) {
        headers2 = growing_buffer_new(pool, 2048);
        headers_copy_all(headers, headers2);
    }

    http_client_request(*pool, connection->fd, ISTREAM_SOCKET,
                        *lease, lease_ctx,
                        nullptr, nullptr,
                        method, uri, headers2, body, expect_100,
                        *handler, ctx, *async_ref);
}

static void
connection_close(struct connection *c)
{
    assert(c != nullptr);
    assert(c->pid >= 1);
    assert(c->fd >= 0);

    close(c->fd);
    c->fd = -1;

    int status;
    if (waitpid(c->pid, &status, 0) < 0) {
        perror("waitpid() failed");
        exit(EXIT_FAILURE);
    }

    assert(!WIFSIGNALED(status));
}

static struct connection *
connect_server(const char *path, const char *mode)
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

    static struct connection c;
    c.pid = pid;
    c.fd = sv[0];

    return &c;
}

static struct connection *
connect_mirror(void)
{
    return connect_server("./test/run_http_server", "mirror");
}

static struct connection *
connect_null(void)
{
    return connect_server("./test/run_http_server", "null");
}

static struct connection *
connect_dummy(void)
{
    return connect_server("./test/run_http_server", "dummy");
}

static struct connection *
connect_fixed(void)
{
    return connect_server("./test/run_http_server", "fixed");
}

static struct connection *
connect_tiny(void)
{
    return connect_fixed();
}

static struct connection *
connect_twice_100(void)
{
    return connect_server("./test/twice_100.sh", nullptr);
}

static struct connection *
connect_close_100(void)
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

    static struct connection c;
    c.pid = pid;
    c.fd = sv[0];
    return &c;
}

static struct connection *
connect_hold(void)
{
    return connect_server("./test/run_http_server", "hold");
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    event_base = event_init();
    fb_pool_init(false);

    pool = pool_new_libc(nullptr, "root");

    run_all_tests(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();
}
