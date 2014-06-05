#include "was_client.hxx"
#include "was_launch.hxx"
#include "lease.h"
#include "http_response.h"
#include "direct.h"
#include "async.h"
#include "istream.h"
#include "istream_file.h"
#include "fb_pool.h"
#include "child_options.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <event.h>
#include <signal.h>

struct context {
    struct was_process process;

    struct istream *body;
    bool error;

    struct async_operation_ref async_ref;
};


/*
 * socket lease
 *
 */

static void
my_lease_release(bool reuse, void *ctx)
{
    struct context *c = (struct context *)ctx;

    (void)reuse;

    kill(c->process.pid, SIGTERM);

    close(c->process.control_fd);
    close(c->process.input_fd);
    close(c->process.output_fd);
}

static const struct lease my_lease = {
    .release = my_lease_release,
};


/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    struct context *c = (struct context *)ctx;
    ssize_t nbytes;

    nbytes = write(1, data, length);
    if (nbytes <= 0) {
        c->error = true;
        istream_free_handler(&c->body);
        return 0;
    }

    return (size_t)nbytes;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = (struct context *)ctx;

    c->body = nullptr;
}

static void
my_istream_abort(GError *error, void *ctx)
{
    struct context *c = (struct context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->body = nullptr;
    c->error = true;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};



/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            struct istream *body gcc_unused,
            void *ctx)
{
    struct context *c = (struct context *)ctx;

    (void)status;

    if (body != nullptr)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct context *c = (struct context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->error = true;
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
};

static struct istream *
request_body(struct pool *pool)
{
    struct stat st;
    return fstat(0, &st) == 0 && S_ISREG(st.st_mode)
        ? istream_file_fd_new(pool, "/dev/stdin", 0, ISTREAM_FILE, -1)
        : nullptr;
}

int main(int argc, char **argv) {
    daemon_log_config.verbose = 5;

    gchar **parameters = nullptr;
    const GOptionEntry option_entries[] = {
        { .long_name = "parameter", .short_name = 'p',
          .arg = G_OPTION_ARG_STRING_ARRAY,
          .arg_data = &parameters,
          .description = "Pass a parameter to the application",
        },
        { .long_name = nullptr }
    };

    GOptionContext *option_context = g_option_context_new("PATH");
    g_option_context_add_main_entries(option_context, option_entries, nullptr);
    g_option_context_set_summary(option_context,
                                 "Command-line interface for WAS applications.");

    GError *error = nullptr;
    if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        return EXIT_FAILURE;
    }

    g_option_context_free(option_context);

    direct_global_init();
    struct event_base *event_base = event_init();
    fb_pool_init(false);

    struct child_options child_options;
    child_options.Init();

    static struct context context;
    if (!was_launch(&context.process, argv[1], nullptr,
                    &child_options,
                    &error)) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 2;
    }

    struct pool *pool = pool_new_libc(nullptr, "root");

    unsigned num_parameters = 0;
    if (parameters != nullptr)
        while (parameters[num_parameters] != nullptr)
            ++num_parameters;

    was_client_request(pool, context.process.control_fd,
                       context.process.input_fd, context.process.output_fd,
                       &my_lease, &context,
                       HTTP_METHOD_GET, "/",
                       nullptr, nullptr, nullptr,
                       nullptr, request_body(pool),
                       { (const char *const*)parameters, num_parameters },
                       &my_response_handler, &context,
                       &context.async_ref);
    pool_unref(pool);

    event_dispatch();

    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();

    return context.error;
}
