#include "was/was_client.hxx"
#include "was/was_launch.hxx"
#include "lease.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "async.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream_file.hxx"
#include "fb_pool.hxx"
#include "spawn/ChildOptions.hxx"
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

struct Context final : Lease {
    WasProcess process;

    IstreamPointer body;
    bool error;

    struct async_operation_ref async_ref;

    Context():body(nullptr) {}

    /* istream handler */

    size_t OnData(gcc_unused const void *data, gcc_unused size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        body.Clear();
    }

    void OnError(GError *gerror) {
        g_printerr("%s\n", gerror->message);
        g_error_free(gerror);

        body.Clear();
        error = true;
    }

    /* virtual methods from class Lease */
    void ReleaseLease(gcc_unused bool reuse) override {
        kill(process.pid, SIGTERM);

        close(process.control_fd);
        close(process.input_fd);
        close(process.output_fd);
    }
};

/*
 * istream handler
 *
 */

size_t
Context::OnData(const void *data, size_t length)
{
    ssize_t nbytes = write(1, data, length);
    if (nbytes <= 0) {
        error = true;
        body.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}

/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            struct istream *body gcc_unused,
            void *ctx)
{
    auto *c = (Context *)ctx;

    (void)status;

    if (body != nullptr)
        c->body.Set(*body, MakeIstreamHandler<Context>::handler, c);
}

static void
my_response_abort(GError *error, void *ctx)
{
    auto *c = (Context *)ctx;

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
        ? istream_file_fd_new(pool, "/dev/stdin", 0, FdType::FD_FILE, -1)
        : nullptr;
}

int main(int argc, char **argv) {
    daemon_log_config.verbose = 5;

    gchar **parameters = nullptr;
    const GOptionEntry option_entries[] = {
        { .long_name = "parameter", .short_name = 'p',
          .flags = 0,
          .arg = G_OPTION_ARG_STRING_ARRAY,
          .arg_data = &parameters,
          .description = "Pass a parameter to the application",
        },
        GOptionEntry()
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

    ChildOptions child_options;
    child_options.Init();

    static Context context;
    if (!was_launch(&context.process, argv[1], nullptr, nullptr,
                    child_options,
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
                       context,
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
