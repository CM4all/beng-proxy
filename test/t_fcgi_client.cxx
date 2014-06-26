#define ENABLE_PREMATURE_CLOSE_HEADERS
#define ENABLE_PREMATURE_CLOSE_BODY

#include "t_client.hxx"
#include "tio.hxx"
#include "fcgi_client.hxx"
#include "fcgi_protocol.h"
#include "http_response.hxx"
#include "async.h"
#include "fd-util.h"
#include "header-writer.h"
#include "lease.h"
#include "direct.h"
#include "fd_util.h"
#include "istream.h"
#include "strmap.h"
#include "strutil.h"
#include "tpool.h"
#include "fb_pool.h"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

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
               const struct http_response_handler *handler,
               void *ctx,
               struct async_operation_ref *async_ref)
{
    fcgi_client_request(pool, connection->fd, ISTREAM_SOCKET,
                        lease, lease_ctx,
                        method, uri, uri, nullptr, nullptr, nullptr,
                        nullptr, "192.168.1.100",
                        headers, body,
                        nullptr,
                        -1,
                        handler, ctx, async_ref);
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
        abort();
    }

    assert(!WIFSIGNALED(status));
}

struct fcgi_request {
    uint16_t id;

    http_method_t method;
    const char *uri;
    struct strmap *headers;

    off_t length;
};

static void
read_fcgi_header(struct fcgi_record_header *header)
{
    read_full(header, sizeof(*header));
    if (header->version != FCGI_VERSION_1)
        abort();
}

static void
read_fcgi_begin_request(struct fcgi_begin_request *begin, uint16_t *request_id)
{
    struct fcgi_record_header header;
    read_fcgi_header(&header);

    if (header.type != FCGI_BEGIN_REQUEST ||
        ntohs(header.content_length) != sizeof(*begin))
        abort();

    *request_id = header.request_id;

    read_full(begin, sizeof(*begin));
    discard(header.padding_length);
}

static size_t
read_fcgi_length(size_t *remaining_r)
{
    uint8_t a = read_byte(remaining_r);
    if (a < 0x80)
        return a;

    uint8_t b = read_byte(remaining_r), c = read_byte(remaining_r),
        d = read_byte(remaining_r);

    return ((a & 0x7f) << 24) | (b << 16) | (c << 8) | d;
}

static void
handle_fcgi_param(struct pool *pool, struct fcgi_request *r,
                  const char *name, const char *value)
{
    if (strcmp(name, "REQUEST_METHOD") == 0) {
        if (strcmp(value, "HEAD") == 0)
            r->method = HTTP_METHOD_HEAD;
        else if (strcmp(value, "POST") == 0)
            r->method = HTTP_METHOD_POST;
    } else if (strcmp(name, "REQUEST_URI") == 0) {
        r->uri = p_strdup(pool, value);
    } else if (memcmp(name, "HTTP_", 5) == 0 && name[5] != 0) {
        char *p = p_strdup(pool, name + 5);

        for (char *q = p; *q != 0; ++q) {
            if (*q == '_')
                *q = '-';
            else if (char_is_capital_letter(*q))
                *q += 'a' - 'A';
        }

        strmap_add(r->headers, p, p_strdup(pool, value));
    }
}

static void
read_fcgi_params(struct pool *pool, struct fcgi_request *r)
{
    r->method = HTTP_METHOD_GET;
    r->uri = nullptr;
    r->headers = strmap_new(pool, 17);

    char name[1024], value[8192];
    while (true) {
        struct fcgi_record_header header;
        read_fcgi_header(&header);

        if (header.type != FCGI_PARAMS ||
            header.request_id != r->id)
            abort();

        size_t remaining = ntohs(header.content_length);
        if (remaining == 0)
            break;

        while (remaining > 0) {
            size_t name_length = read_fcgi_length(&remaining),
                value_length = read_fcgi_length(&remaining);

            if (name_length >= sizeof(name) || value_length >= sizeof(value) ||
                name_length + value_length > remaining)
                abort();

            read_full(name, name_length);
            name[name_length] = 0;
            remaining -= name_length;

            read_full(value, value_length);
            value[value_length] = 0;
            remaining -= value_length;

            handle_fcgi_param(pool,r, name, value);
        }

        discard(header.padding_length);
    }
}

static void
read_fcgi_request(struct pool *pool, struct fcgi_request *r)
{
    struct fcgi_begin_request begin;
    read_fcgi_begin_request(&begin, &r->id);
    if (ntohs(begin.role) != FCGI_RESPONDER)
        abort();

    read_fcgi_params(pool, r);

    const char *content_length = strmap_remove(r->headers, "content-length");
    r->length = content_length != nullptr
        ? strtol(content_length, nullptr, 10)
        : -1;

    if (content_length == nullptr) {
        struct fcgi_record_header header;
        ssize_t nbytes = recv(0, &header, sizeof(header),
                              MSG_DONTWAIT|MSG_PEEK);
        if (nbytes == (ssize_t)sizeof(header) &&
            header.version == FCGI_VERSION_1 &&
            header.type == FCGI_STDIN &&
            header.content_length == 0)
            r->length = 0;
    }
}

static void
discard_fcgi_request_body(struct fcgi_request *r)
{
    struct fcgi_record_header header;

    while (true) {
        read_fcgi_header(&header);

        if (header.type != FCGI_STDIN ||
            header.request_id != r->id)
            abort();

        size_t length = ntohs(header.content_length);
        if (length == 0)
            break;

        discard(length);
    }
}

static void
write_fcgi_stdout(const struct fcgi_request *r,
                  const void *data, size_t length)
{
    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = r->id,
        .content_length = htons(length),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));
    write_full(data, length);
}

static void
write_fcgi_stdout_string(const struct fcgi_request *r,
                         const char *data)
{
    write_fcgi_stdout(r, data, strlen(data));
}

static void
write_fcgi_headers(const struct fcgi_request *r, http_status_t status,
                   struct strmap *headers)
{
    char buffer[8192], *p = buffer;
    p += sprintf(p, "status: %u\n", status);

    if (headers != nullptr) {
        strmap_rewind(headers);
        const struct strmap_pair *pair;
        while ((pair = strmap_next(headers)) != nullptr)
            p += sprintf(p, "%s: %s\n", pair->key, pair->value);
    }

    p += sprintf(p, "\n");

    write_fcgi_stdout(r, buffer, p - buffer);
}

static void
write_fcgi_end(const struct fcgi_request *r)
{
    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_END_REQUEST,
        .request_id = r->id,
        .content_length = 0,
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));
}

static struct connection *
connect_server(void (*f)(struct pool *pool))
{
    int sv[2];
    pid_t pid;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair() failed");
        abort();
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        abort();
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

    static struct connection c;
    c.pid = pid;
    c.fd = sv[0];
    return &c;
}

static void
fcgi_server_null(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);
    write_fcgi_headers(&request, HTTP_STATUS_NO_CONTENT, nullptr);
    write_fcgi_end(&request);
    discard_fcgi_request_body(&request);
}

static struct connection *
connect_null(void)
{
    return connect_server(fcgi_server_null);
}

static void
fcgi_server_hello(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);

    write_fcgi_headers(&request, HTTP_STATUS_OK, nullptr);
    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "hello");
    write_fcgi_end(&request);
}

static struct connection *
connect_hello(void)
{
    return connect_server(fcgi_server_hello);
}

static struct connection *
connect_dummy(void)
{
    return connect_hello();
}

static struct connection *
connect_fixed(void)
{
    return connect_hello();
}

static void
fcgi_server_tiny(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);

    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "content-length: 5\n\nhello");
    write_fcgi_end(&request);
}

static struct connection *
connect_tiny(void)
{
    return connect_server(fcgi_server_tiny);
}

static void
mirror_data(size_t length)
{
    char buffer[4096];

    while (length > 0) {
        size_t l = length;
        if (l > sizeof(buffer))
            l = sizeof(buffer);

        ssize_t nbytes = recv(0, buffer, l, MSG_WAITALL);
        if (nbytes <= 0)
            exit(EXIT_FAILURE);

        write_full(buffer, nbytes);
        length -= nbytes;
    }
}

static void
fcgi_server_mirror(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);

    http_status_t status = request.length == 0
        ? HTTP_STATUS_NO_CONTENT
        : HTTP_STATUS_OK;

    if (request.length > 0) {
        char buffer[32];
        sprintf(buffer, "%llu", (unsigned long long)request.length);
        strmap_add(request.headers, "content-length", buffer);
    }

    write_fcgi_headers(&request, status, request.headers);

    if (request.method == HTTP_METHOD_HEAD)
        discard_fcgi_request_body(&request);
    else {
        while (true) {
            struct fcgi_record_header header;
            read_fcgi_header(&header);

            if (header.type != FCGI_STDIN ||
                header.request_id != request.id)
                abort();

            if (header.content_length == 0)
                break;

            header.type = FCGI_STDOUT;
            write_full(&header, sizeof(header));
            mirror_data(ntohs(header.content_length) + header.padding_length);
        }
    }

    write_fcgi_end(&request);
}

static struct connection *
connect_mirror(void)
{
    return connect_server(fcgi_server_mirror);
}

static void
fcgi_server_hold(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);
    write_fcgi_headers(&request, HTTP_STATUS_OK, nullptr);

    /* wait until the connection gets closed */
    struct fcgi_record_header header;
    read_fcgi_header(&header);
}

static struct connection *
connect_hold(void)
{
    return connect_server(fcgi_server_hold);
}

static void
fcgi_server_premature_close_headers(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);
    discard_fcgi_request_body(&request);

    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = request.id,
        .content_length = htons(1024),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));

    const char *data = "Foo: 1\nBar: 1\nX: ";
    write_full(data, strlen(data));
}

static struct connection *
connect_premature_close_headers(void)
{
    return connect_server(fcgi_server_premature_close_headers);
}

static void
fcgi_server_premature_close_body(struct pool *pool)
{
    struct fcgi_request request;
    read_fcgi_request(pool, &request);
    discard_fcgi_request_body(&request);

    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = request.id,
        .content_length = htons(1024),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));

    const char *data = "Foo: 1\nBar: 1\n\nFoo Bar";
    write_full(data, strlen(data));
}

static struct connection *
connect_premature_close_body(void)
{
    return connect_server(fcgi_server_premature_close_body);
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
    tpool_init(pool);

    run_all_tests(pool);

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();

    int status;
    while (wait(&status) > 0) {
        assert(!WIFSIGNALED(status));
    }
}
