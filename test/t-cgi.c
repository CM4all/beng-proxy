#include "cgi.h"
#include "async.h"
#include "http-response.h"
#include "child.h"
#include "direct.h"
#include "crash.h"
#include "istream-file.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

struct context {
    struct async_operation_ref async_ref;

    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    bool body_read, no_content;
    int fd;
    bool released, aborted;
    http_status_t status;

    istream_t body;
    off_t body_data, body_available;
    bool body_eof, body_abort, body_closed;
};

static istream_direct_t my_handler_direct = 0;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data gcc_unused, size_t length, void *ctx)
{
    struct context *c = ctx;

    c->body_data += length;

    if (c->close_response_body_data) {
        c->body_closed = true;
        istream_free_handler(&c->body);
        children_shutdown();
        return 0;
    }

    if (c->data_blocking) {
        --c->data_blocking;
        return 0;
    }

    return length;
}

static ssize_t
my_istream_direct(G_GNUC_UNUSED istream_direct_t type, int fd,
                  size_t max_length, void *ctx)
{
    struct context *c = ctx;

    if (c->close_response_body_data) {
        c->body_closed = true;
        istream_free_handler(&c->body);
        children_shutdown();
        return 0;
    }

    if (c->data_blocking) {
        --c->data_blocking;
        return -2;
    }

    char buffer[256];
    if (max_length > sizeof(buffer))
        max_length = sizeof(buffer);

    ssize_t nbytes = read(fd, buffer, max_length);
    if (nbytes <= 0)
        return nbytes;

    c->body_data += nbytes;
    return nbytes;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;

    children_shutdown();
}

static void
my_istream_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_error_free(error);

    c->body = NULL;
    c->body_abort = true;

    children_shutdown();
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .direct = my_istream_direct,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            istream_t body,
            void *ctx)
{
    struct context *c = ctx;

    assert(!c->no_content || body == NULL);

    c->status = status;

    if (c->close_response_body_early) {
        istream_close_unused(body);
        children_shutdown();
    } else if (body != NULL) {
        istream_assign_handler(&c->body, body, &my_istream_handler, c,
                               my_handler_direct);
        c->body_available = istream_available(body, false);
    }

    if (c->close_response_body_late) {
        c->body_closed = true;
        istream_free_handler(&c->body);
        children_shutdown();
    }

    if (c->body_read) {
        assert(body != NULL);
        istream_read(body);
    }

    if (c->no_content)
        children_shutdown();
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->aborted = true;

    children_shutdown();
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
};


/*
 * tests
 *
 */

static void
test_normal(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "env.py", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_early(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    c->close_response_body_early = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "env.py", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_late(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    c->close_response_body_late = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "env.py", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort || c->body_closed);
}

static void
test_close_data(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";
    c->close_response_body_data = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "env.py", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->body_closed);
}

static void
test_post(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/cat.sh", NULL);
    else
        path = "./demo/cgi-bin/cat.sh";

    c->body_read = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_POST, "/",
            "cat.sh", NULL, NULL, "/var/www", NULL,
            NULL, istream_file_new(pool, "Makefile", 8192),
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_status(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/status.sh", NULL);
    else
        path = "./demo/cgi-bin/status.sh";

    c->body_read = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "status.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_CREATED);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_no_content(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/no_content.sh", NULL);
    else
        path = "./demo/cgi-bin/no_content.sh";

    c->no_content = true;

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "no_content.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_no_length(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length0.sh", NULL);
    else
        path = "./demo/cgi-bin/length0.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length0.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == -1);
    assert(c->body_eof);
}

static void
test_length_ok(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length1.sh", NULL);
    else
        path = "./demo/cgi-bin/length1.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length1.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == 4);
    assert(c->body_eof);
}

static void
test_length_ok_large(struct pool *pool, struct context *c)
{
    const char *path;

    c->body_read = true;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length5.sh", NULL);
    else
        path = "./demo/cgi-bin/length5.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length5.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == 8192);
    assert(c->body_eof);
}

static void
test_length_too_small(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length2.sh", NULL);
    else
        path = "./demo/cgi-bin/length2.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length2.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->aborted);
}

static void
test_length_too_big(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length3.sh", NULL);
    else
        path = "./demo/cgi-bin/length3.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length3.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(!c->aborted);
    assert(c->body_abort);
}

static void
test_length_too_small_late(struct pool *pool, struct context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length4.sh", NULL);
    else
        path = "./demo/cgi-bin/length4.sh";

    cgi_new(pool, false, NULL, NULL,
            path,
            HTTP_METHOD_GET, "/",
            "length4.sh", NULL, NULL, "/var/www",
            NULL, NULL, NULL,
            NULL, 0,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(!c->aborted);
    assert(c->body_abort);
}


/*
 * main
 *
 */

static void
run_test(struct pool *pool, void (*test)(struct pool *pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));

    children_init(pool);

    pool = pool_new_linear(pool, "test", 16384);
    test(pool, &c);
    pool_commit();
}

static void
run_all_tests(struct pool *pool)
{
    run_test(pool, test_normal);
    run_test(pool, test_close_early);
    run_test(pool, test_close_late);
    run_test(pool, test_close_data);
    run_test(pool, test_post);
    run_test(pool, test_status);
    run_test(pool, test_no_content);
    run_test(pool, test_no_length);
    run_test(pool, test_length_ok);
    run_test(pool, test_length_ok_large);
    run_test(pool, test_length_too_small);
    run_test(pool, test_length_too_big);
    run_test(pool, test_length_too_small_late);
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    crash_global_init();
    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    run_all_tests(pool);

    my_handler_direct = ISTREAM_ANY;
    run_all_tests(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    crash_global_deinit();
    direct_global_deinit();
}
