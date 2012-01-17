/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi.h"
#include "cgi-client.h"
#include "istream.h"
#include "processor.h"
#include "fork.h"
#include "strutil.h"
#include "abort-flag.h"
#include "stopwatch.h"
#include "jail.h"
#include "strmap.h"
#include "http-response.h"

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
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        const char *remote_addr,
        struct strmap *headers,
        off_t content_length,
        const char *const params[], unsigned num_params)
{
    const struct strmap_pair *pair;
    const char *arg = NULL;

    assert(path != NULL);
    assert(http_method_is_valid(method));
    assert(uri != NULL);

    if (script_name == NULL)
        script_name = "";

    if (path_info == NULL)
        path_info = "";

    if (query_string == NULL)
        query_string = "";

    if (document_root == NULL)
        document_root = "/var/www";

    clearenv();

    for (unsigned j = 0; j < num_params; ++j) {
        union {
            const char *in;
            char *out;
        } u = {
            .in = params[j],
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
    setenv("SERVER_SOFTWARE", "beng-proxy v" VERSION, 1);

    if (remote_addr != NULL)
        setenv("REMOTE_ADDR", remote_addr, 1);

    if (jail != NULL && jail->enabled) {
        setenv("JAILCGI_FILENAME", path, 1);
        path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

        if (jail->home_directory != NULL)
            setenv("JETSERV_HOME", jail->home_directory, 1);

        if (interpreter != NULL)
            setenv("JAILCGI_INTERPRETER", interpreter, 1);

        if (action != NULL)
            setenv("JAILCGI_ACTION", action, 1);
    } else {
        if (action != NULL)
            path = action;

        if (interpreter != NULL) {
            arg = path;
            path = interpreter;
        }
    }

    const char *content_type = NULL;
    if (headers != NULL) {
        strmap_rewind(headers);
        while ((pair = strmap_next(headers)) != NULL) {
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

    if (content_type != NULL)
        setenv("CONTENT_TYPE", content_type, 1);

    if (content_length >= 0) {
        char value[32];
        snprintf(value, sizeof(value), "%llu",
                 (unsigned long long)content_length);
        setenv("CONTENT_LENGTH", value, 1);
    }

    execl(path, path, arg, NULL);
    fprintf(stderr, "exec('%s') failed: %s\n",
            path, strerror(errno));
    _exit(2);
}

static void
cgi_child_callback(int status, void *ctx gcc_unused)
{
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        daemon_log(1, "CGI died from signal %d%s\n",
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status != 0)
        daemon_log(1, "CGI exited with status %d\n",
                   exit_status);
}

void
cgi_new(struct pool *pool, const struct jail_params *jail,
        const char *interpreter, const char *action,
        const char *path,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        const char *remote_addr,
        struct strmap *headers, struct istream *body,
        const char *const params[], unsigned num_params,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref)
{
    struct stopwatch *stopwatch = stopwatch_new(pool, path);

    off_t available = body != NULL
        ? istream_available(body, false)
        : -1;

    stopwatch = stopwatch_new(pool, path);

    struct istream *input;
    GError *error = NULL;
    pid_t pid = beng_fork(pool, body, &input,
                          cgi_child_callback, NULL, &error);
    if (pid < 0) {
        if (body != NULL) {
            /* beng_fork() left the request body open - free this
               resource, because our caller always assume that we have
               consumed it */
            struct abort_flag abort_flag;
            abort_flag_set(&abort_flag, async_ref);

            istream_close_unused(body);

            if (abort_flag.aborted) {
                /* the operation was aborted - don't call the
                   http_response_handler */
                g_error_free(error);
                return;
            }
        }

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (pid == 0)
        cgi_run(jail, interpreter, action, path, method, uri,
                script_name, path_info, query_string, document_root,
                remote_addr,
                headers, available,
                params, num_params);

    stopwatch_event(stopwatch, "fork");

    cgi_client_new(pool, stopwatch, input, handler, handler_ctx, async_ref);
}
