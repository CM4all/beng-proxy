#define ENABLE_PREMATURE_CLOSE_HEADERS
#define ENABLE_PREMATURE_CLOSE_BODY
#define NO_EARLY_RELEASE_SOCKET // TODO: improve the AJP client

#include "tio.hxx"
#include "t_client.hxx"
#include "ajp_server.hxx"
#include "ajp/ajp_client.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "system/SetupProcess.hxx"
#include "system/fd-util.h"
#include "system/fd_util.h"
#include "growing_buffer.hxx"
#include "header_writer.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "event/Event.hxx"
#include "util/ByteOrder.hxx"

#include <inline/compiler.h>

#include <sys/wait.h>

static void
ajp_server_null(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    write_headers(HTTP_STATUS_NO_CONTENT, nullptr);
    write_end();
}

static void
ajp_server_hello(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    write_headers(HTTP_STATUS_OK, nullptr);
    write_body_chunk("hello", 5, 0);
    write_end();
}

static void
ajp_server_tiny(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    auto *headers = strmap_new(pool);
    headers->Add("content-length", "5");

    write_headers(HTTP_STATUS_OK, headers);
    write_body_chunk("hello", 5, 0);
    write_end();
}

static void
ajp_server_mirror(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    http_status_t status = request.length == 0
        ? HTTP_STATUS_NO_CONTENT
        : HTTP_STATUS_OK;

    write_headers(status, request.headers);

    if (request.method != AJP_METHOD_HEAD) {
        size_t position = 0;
        while (position < request.length) {
            if (request.received < request.length && position == request.received)
                read_ajp_request_body_chunk(&request);

            assert(position < request.received);

            size_t nbytes = request.received - position;
            if (nbytes > 8192)
                nbytes = 8192;

            write_body_chunk(request.body + position, nbytes, 0);
            position += nbytes;
        }

        if (request.length > 0)
            read_ajp_end_request_body_chunk(&request);
    }

    write_end();
}

static void
ajp_server_hold(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);
    write_headers(HTTP_STATUS_OK, nullptr);

    /* wait until the connection gets closed */
    struct ajp_header header;
    read_ajp_header(&header);
}

static void
ajp_server_premature_close_headers(gcc_unused struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(256),
    };

    write_full(&header, sizeof(header));
}

static void
ajp_server_premature_close_body(gcc_unused struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    write_headers(HTTP_STATUS_OK, nullptr);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(256),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_BODY_CHUNK);
    write_short(200);
}

struct Connection {
    EventLoop &event_loop;
    const pid_t pid;
    const int fd;

    Connection(EventLoop &_event_loop, pid_t _pid, int _fd)
        :event_loop(_event_loop), pid(_pid), fd(_fd) {}
    static Connection *New(EventLoop &event_loop, void (*f)(struct pool *pool));
    ~Connection();

    void Request(struct pool *pool,
                 Lease &lease,
                 http_method_t method, const char *uri,
                 StringMap &headers,
                 Istream *body,
                 const struct http_response_handler *handler,
                 void *ctx,
                 struct async_operation_ref *async_ref) {
        ajp_client_request(*pool, event_loop, fd, FdType::FD_SOCKET,
                           lease,
                           "http", "192.168.1.100", "remote", "server", 80, false,
                           method, uri, headers, body,
                           *handler, ctx, *async_ref);
    }

    static Connection *NewMirror(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_mirror);
    }

    static Connection *NewNull(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_null);
    }

    static Connection *NewDummy(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_hello);
    }

    static Connection *NewFixed(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_hello);
    }

    static Connection *NewTiny(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_tiny);
    }

    static Connection *NewHold(struct pool &, EventLoop &event_loop) {
        return New(event_loop, ajp_server_hold);
    }

    static Connection *NewPrematureCloseHeaders(struct pool &,
                                                EventLoop &event_loop) {
        return New(event_loop, ajp_server_premature_close_headers);
    }

    static Connection *NewPrematureCloseBody(struct pool &,
                                             EventLoop &event_loop) {
        return New(event_loop, ajp_server_premature_close_body);
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
        abort();
    }

    assert(!WIFSIGNALED(status));
}

Connection *
Connection::New(EventLoop &event_loop, void (*f)(struct pool *pool))
{
    int sv[2];
    pid_t pid;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
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
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);

        struct pool *pool = pool_new_libc(nullptr, "f");
        f(pool);
        shutdown(0, SHUT_RDWR);
        pool_unref(pool);
        exit(EXIT_SUCCESS);
    }

    close(sv[1]);

    fd_set_nonblock(sv[0], 1);

    return new Connection(event_loop, pid, sv[0]);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SetupProcess();

    direct_global_init();
    EventLoop event_loop;
    fb_pool_init(event_loop, false);

    run_all_tests<Connection>(RootPool());

    fb_pool_deinit();

    int status;
    while (wait(&status) > 0) {
        assert(!WIFSIGNALED(status));
    }
}
