#include "tconstruct.hxx"
#include "cgi/cgi_glue.hxx"
#include "cgi_address.hxx"
#include "async.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "crash.hxx"
#include "istream/istream_file.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "event/Event.hxx"
#include "spawn/Config.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "system/SetupProcess.hxx"

#include <inline/compiler.h>

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static SpawnConfig spawn_config;

struct Context final : IstreamHandler {
    ChildProcessRegistry child_process_registry;
    LocalSpawnService spawn_service;

    struct async_operation_ref async_ref;

    unsigned data_blocking = 0;
    bool close_response_body_early = false;
    bool close_response_body_late = false;
    bool close_response_body_data = false;
    bool body_read = false, no_content = false;
    bool released = false, aborted = false;
    http_status_t status = http_status_t(0);

    IstreamPointer body;
    off_t body_data = 0, body_available = 0;
    bool body_eof = false, body_abort = false, body_closed = false;

    Context()
        :spawn_service(spawn_config, child_process_registry), body(nullptr) {
        child_process_registry.SetVolatile();
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

static FdTypeMask my_handler_direct = 0;

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length)
{
    body_data += length;

    if (close_response_body_data) {
        body_closed = true;
        body.ClearAndClose();
        return 0;
    }

    if (data_blocking) {
        --data_blocking;
        return 0;
    }

    return length;
}

ssize_t
Context::OnDirect(gcc_unused FdType type, int fd, size_t max_length)
{
    if (close_response_body_data) {
        body_closed = true;
        body.ClearAndClose();
        return 0;
    }

    if (data_blocking) {
        --data_blocking;
        return ISTREAM_RESULT_BLOCKING;
    }

    char buffer[256];
    if (max_length > sizeof(buffer))
        max_length = sizeof(buffer);

    ssize_t nbytes = read(fd, buffer, max_length);
    if (nbytes <= 0)
        return nbytes;

    body_data += nbytes;
    return nbytes;
}

void
Context::OnEof()
{
    body.Clear();
    body_eof = true;
}

void
Context::OnError(GError *error)
{
    g_error_free(error);

    body.Clear();
    body_abort = true;
}

/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            Istream *body,
            void *ctx)
{
    auto *c = (Context *)ctx;

    assert(!c->no_content || body == NULL);

    c->status = status;

    if (c->close_response_body_early) {
        body->CloseUnused();
    } else if (body != NULL) {
        c->body.Set(*body, *c, my_handler_direct);
        c->body_available = body->GetAvailable(false);
    }

    if (c->close_response_body_late) {
        c->body_closed = true;
        c->body.ClearAndClose();
    }

    if (c->body_read) {
        assert(body != NULL);
        c->body.Read();
    }
}

static void
my_response_abort(GError *error, void *ctx)
{
    auto *c = (Context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->aborted = true;
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
test_normal(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("env.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body.IsDefined());
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_tiny(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/tiny.sh", NULL);
    else
        path = "./demo/cgi-bin/tiny.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("tiny.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body.IsDefined());
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_early(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    c->close_response_body_early = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("env.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body.IsDefined());
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_late(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";

    c->close_response_body_late = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("env.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body.IsDefined());
    assert(!c->body_eof);
    assert(c->body_abort || c->body_closed);
}

static void
test_close_data(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/env.py", NULL);
    else
        path = "./demo/cgi-bin/env.py";
    c->close_response_body_data = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("env.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
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
test_post(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/cat.sh", NULL);
    else
        path = "./demo/cgi-bin/cat.sh";

    c->body_read = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("cat.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_POST, &address,
            NULL, NULL, istream_file_new(pool, "Makefile", 8192, NULL),
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_OK);
    assert(!c->body.IsDefined());
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_status(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/status.sh", NULL);
    else
        path = "./demo/cgi-bin/status.sh";

    c->body_read = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("status.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_CREATED);
    assert(!c->body.IsDefined());
    assert(c->body_eof);
    assert(!c->body_abort);
}

static void
test_no_content(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/no_content.sh", NULL);
    else
        path = "./demo/cgi-bin/no_content.sh";

    c->no_content = true;

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("no_content.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(!c->body.IsDefined());
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_no_length(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length0.sh", NULL);
    else
        path = "./demo/cgi-bin/length0.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length0.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == -1);
    assert(c->body_eof);
}

static void
test_length_ok(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length1.sh", NULL);
    else
        path = "./demo/cgi-bin/length1.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length1.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == 4);
    assert(c->body_eof);
}

static void
test_length_ok_large(struct pool *pool, Context *c)
{
    const char *path;

    c->body_read = true;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length5.sh", NULL);
    else
        path = "./demo/cgi-bin/length5.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length5.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->body_available == 8192);
    assert(c->body_eof);
}

static void
test_length_too_small(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length2.sh", NULL);
    else
        path = "./demo/cgi-bin/length2.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length2.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->aborted);
}

static void
test_length_too_big(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length3.sh", NULL);
    else
        path = "./demo/cgi-bin/length3.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length3.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(!c->aborted);
    assert(c->body_abort);
}

static void
test_length_too_small_late(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/length4.sh", NULL);
    else
        path = "./demo/cgi-bin/length4.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("length4.sh")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(!c->aborted);
    assert(c->body_abort);
}

/**
 * Test a response header that is too large for the buffer.
 */
static void
test_large_header(struct pool *pool, Context *c)
{
    const char *path;

    path = getenv("srcdir");
    if (path != NULL)
        path = p_strcat(pool, path, "/demo/cgi-bin/large_header.sh", NULL);
    else
        path = "./demo/cgi-bin/large_header.sh";

    const auto address = MakeCgiAddress(path, "/")
        .ScriptName("large_header.py")
        .DocumentRoot("/var/www");

    cgi_new(c->spawn_service, pool, HTTP_METHOD_GET, &address,
            NULL, NULL, NULL,
            &my_response_handler, c,
            &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->aborted);
    assert(!c->body_abort);
}


/*
 * main
 *
 */

static void
run_test(void (*test)(struct pool *pool, Context *c))
{
    Context c;

    RootPool root_pool;
    auto pool = pool_new_linear(root_pool, "test", 16384);
    test(pool, &c);
}

static void
run_all_tests()
{
    run_test(test_normal);
    run_test(test_tiny);
    run_test(test_close_early);
    run_test(test_close_late);
    run_test(test_close_data);
    run_test(test_post);
    run_test(test_status);
    run_test(test_no_content);
    run_test(test_no_length);
    run_test(test_length_ok);
    run_test(test_length_ok_large);
    run_test(test_length_too_small);
    run_test(test_length_too_big);
    run_test(test_length_too_small_late);
    run_test(test_large_header);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SetupProcess();

    direct_global_init();
    crash_global_init();
    EventBase event_base;
    fb_pool_init(false);

    run_all_tests();

    my_handler_direct = FD_ANY;
    run_all_tests();

    fb_pool_deinit();
    crash_global_deinit();
}
