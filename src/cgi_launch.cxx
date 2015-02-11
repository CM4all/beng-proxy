/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_launch.hxx"
#include "cgi_address.hxx"
#include "istream.h"
#include "fork.hxx"
#include "strmap.hxx"
#include "sigutil.h"
#include "product.h"
#include "exec.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"

#include <daemon/log.h>

#include <sys/wait.h>
#include <assert.h>
#include <string.h>

static void gcc_noreturn
cgi_run(const struct jail_params *jail,
        const char *interpreter, const char *action,
        const char *path,
        ConstBuffer<const char *> args,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        const char *remote_addr,
        const struct strmap *headers,
        off_t content_length,
        ConstBuffer<const char *> env)
{
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

    Exec e;

    for (auto i : env)
        e.PutEnv(i);

    e.SetEnv("GATEWAY_INTERFACE", "CGI/1.1");
    e.SetEnv("SERVER_PROTOCOL", "HTTP/1.1");
    e.SetEnv("REQUEST_METHOD", http_method_to_string(method));
    e.SetEnv("SCRIPT_FILENAME", path);
    e.SetEnv("PATH_TRANSLATED", path);
    e.SetEnv("REQUEST_URI", uri);
    e.SetEnv("SCRIPT_NAME", script_name);
    e.SetEnv("PATH_INFO", path_info);
    e.SetEnv("QUERY_STRING", query_string);
    e.SetEnv("DOCUMENT_ROOT", document_root);
    e.SetEnv("SERVER_SOFTWARE", PRODUCT_TOKEN);

    if (remote_addr != nullptr)
        e.SetEnv("REMOTE_ADDR", remote_addr);

    if (jail != nullptr && jail->enabled) {
        e.SetEnv("JAILCGI_FILENAME", path);
        path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

        if (jail->home_directory != nullptr)
            e.SetEnv("JETSERV_HOME", jail->home_directory);

        if (interpreter != nullptr)
            e.SetEnv("JAILCGI_INTERPRETER", interpreter);

        if (action != nullptr)
            e.SetEnv("JAILCGI_ACTION", action);
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
        for (const auto &pair : *headers) {
            if (strcmp(pair.key, "content-type") == 0) {
                content_type = pair.value;
                continue;
            }

            char buffer[512] = "HTTP_";
            size_t i;
            for (i = 0; 5 + i < sizeof(buffer) - 1 && pair.key[i] != 0; ++i) {
                if (IsLowerAlphaASCII(pair.key[i]))
                    buffer[5 + i] = (char)(pair.key[i] - 'a' + 'A');
                else if (IsUpperAlphaASCII(pair.key[i]) ||
                         IsDigitASCII(pair.key[i]))
                    buffer[5 + i] = pair.key[i];
                else
                    buffer[5 + i] = '_';
            }

            buffer[5 + i] = 0;
            e.SetEnv(buffer, pair.value);
        }
    }

    if (content_type != nullptr)
        e.SetEnv("CONTENT_TYPE", content_type);

    if (content_length >= 0) {
        char value[32];
        snprintf(value, sizeof(value), "%llu",
                 (unsigned long long)content_length);
        e.SetEnv("CONTENT_LENGTH", value);
    }

    e.Append(path);
    for (auto i : args)
        e.Append(i);
    if (arg != nullptr)
        e.Append(arg);
    e.DoExec();
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

    address->options.ns.Setup();
    rlimit_options_apply(&address->options.rlimits);

    cgi_run(&address->options.jail,
            address->interpreter, address->action,
            address->path,
            { address->args.values, address->args.n },
            c->method, c->uri,
            address->script_name, address->path_info,
            address->query_string, address->document_root,
            c->remote_addr,
            c->headers, c->available,
            { address->env.values, address->env.n });
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
        .uri = address->GetURI(pool),
        .available = body != nullptr ? istream_available(body, false) : -1,
        .remote_addr = remote_addr,
        .headers = headers,
    };

    const int clone_flags = address->options.ns.GetCloneFlags(SIGCHLD);

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
        return nullptr;
    }

    leave_signal_section(&c.signals);

    return input;
}
