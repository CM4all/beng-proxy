/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_launch.hxx"
#include "cgi_address.hxx"
#include "istream.h"
#include "fork.h"
#include "strutil.h"
#include "strmap.h"
#include "sigutil.h"
#include "product.h"
#include "exec.h"

#include <daemon/log.h>

#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static void gcc_noreturn
cgi_run(const struct jail_params *jail,
        const char *interpreter, const char *action,
        const char *path,
        const char *const*args, unsigned n_args,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        const char *remote_addr,
        struct strmap *headers,
        off_t content_length,
        const char *const env[], unsigned num_env)
{
    const struct strmap_pair *pair;
    const char *arg = nullptr;

    assert(path != nullptr);
    assert(http_method_is_valid(method));
    assert(uri != nullptr);

    if (script_name == nullptr)
        script_name = "";

    if (path_info == nullptr)
        path_info = "";

    if (query_string == nullptr)
        query_string = "";

    if (document_root == nullptr)
        document_root = "/var/www";

    clearenv();

    for (unsigned j = 0; j < num_env; ++j) {
        union {
            const char *in;
            char *out;
        } u = {
            .in = env[j],
        };

        putenv(u.out);
    }

    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
    setenv("REQUEST_METHOD", http_method_to_string(method), 1);
    setenv("SCRIPT_FILENAME", path, 1);
    setenv("PATH_TRANSLATED", path, 1);
    setenv("REQUEST_URI", uri, 1);
    setenv("SCRIPT_NAME", script_name, 1);
    setenv("PATH_INFO", path_info, 1);
    setenv("QUERY_STRING", query_string, 1);
    setenv("DOCUMENT_ROOT", document_root, 1);
    setenv("SERVER_SOFTWARE", PRODUCT_TOKEN, 1);

    if (remote_addr != nullptr)
        setenv("REMOTE_ADDR", remote_addr, 1);

    if (jail != nullptr && jail->enabled) {
        setenv("JAILCGI_FILENAME", path, 1);
        path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

        if (jail->home_directory != nullptr)
            setenv("JETSERV_HOME", jail->home_directory, 1);

        if (interpreter != nullptr)
            setenv("JAILCGI_INTERPRETER", interpreter, 1);

        if (action != nullptr)
            setenv("JAILCGI_ACTION", action, 1);
    } else {
        if (action != nullptr)
            path = action;

        if (interpreter != nullptr) {
            arg = path;
            path = interpreter;
        }
    }

    const char *content_type = nullptr;
    if (headers != nullptr) {
        strmap_rewind(headers);
        while ((pair = strmap_next(headers)) != nullptr) {
            if (strcmp(pair->key, "content-type") == 0) {
                content_type = pair->value;
                continue;
            }

            char buffer[512] = "HTTP_";
            size_t i;
            for (i = 0; 5 + i < sizeof(buffer) - 1 && pair->key[i] != 0; ++i) {
                if (char_is_minuscule_letter(pair->key[i]))
                    buffer[5 + i] = (char)(pair->key[i] - 'a' + 'A');
                else if (char_is_capital_letter(pair->key[i]) ||
                         char_is_digit(pair->key[i]))
                    buffer[5 + i] = pair->key[i];
                else
                    buffer[5 + i] = '_';
            }

            buffer[5 + i] = 0;
            setenv(buffer, pair->value, 1);
        }
    }

    if (content_type != nullptr)
        setenv("CONTENT_TYPE", content_type, 1);

    if (content_length >= 0) {
        char value[32];
        snprintf(value, sizeof(value), "%llu",
                 (unsigned long long)content_length);
        setenv("CONTENT_LENGTH", value, 1);
    }

    struct exec e;
    exec_init(&e);
    exec_append(&e, path);
    for (unsigned i = 0; i < n_args; ++i)
        exec_append(&e, args[i]);
    if (arg != nullptr)
        exec_append(&e, arg);
    exec_do(&e);

    fprintf(stderr, "exec('%s') failed: %s\n",
            path, strerror(errno));
    _exit(2);
}

struct cgi_ctx {
    http_method_t method;
    const struct cgi_address *address;
    const char *uri;
    off_t available;
    const char *remote_addr;
    struct strmap *headers;

    sigset_t signals;
};

static int
cgi_fn(void *ctx)
{
    struct cgi_ctx *c = (struct cgi_ctx *)ctx;
    const struct cgi_address *address = c->address;

    install_default_signal_handlers();
    leave_signal_section(&c->signals);

    address->options.SetupStderr();

    namespace_options_setup(&address->options.ns);
    rlimit_options_apply(&address->options.rlimits);

    cgi_run(&address->options.jail,
            address->interpreter, address->action,
            address->path,
            address->args.values, address->args.n,
            c->method, c->uri,
            address->script_name, address->path_info,
            address->query_string, address->document_root,
            c->remote_addr,
            c->headers, c->available,
            address->env.values, address->env.n);
}

static void
cgi_child_callback(int status, void *ctx gcc_unused)
{
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 4;

        daemon_log(level, "CGI died from signal %d%s\n",
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status != 0)
        daemon_log(1, "CGI exited with status %d\n",
                   exit_status);
}

static const char *
cgi_address_name(const struct cgi_address *address)
{
    if (address->interpreter != nullptr)
        return address->interpreter;

    if (address->action != nullptr)
        return address->action;

    if (address->path != nullptr)
        return address->path;

    return "CGI";
}

struct istream *
cgi_launch(struct pool *pool, http_method_t method,
           const struct cgi_address *address,
           const char *remote_addr,
           struct strmap *headers, struct istream *body,
           GError **error_r)
{
    struct cgi_ctx c = {
        .method = method,
        .address = address,
        .uri = cgi_address_uri(pool, address),
        .available = body != nullptr ? istream_available(body, false) : -1,
        .remote_addr = remote_addr,
        .headers = headers,
    };

    const int clone_flags =
        namespace_options_clone_flags(&address->options.ns, SIGCHLD);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&c.signals);

    struct istream *input;
    pid_t pid = beng_fork(pool, cgi_address_name(address), body, &input,
                          clone_flags,
                          cgi_fn, &c,
                          cgi_child_callback, nullptr, error_r);
    if (pid < 0) {
        leave_signal_section(&c.signals);

        if (body != nullptr)
            /* beng_fork() left the request body open - free this
               resource, because our caller always assume that we have
               consumed it */
            istream_close_unused(body);

        return nullptr;
    }

    leave_signal_section(&c.signals);

    return input;
}
