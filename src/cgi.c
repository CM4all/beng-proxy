/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi.h"
#include "istream-buffer.h"
#include "processor.h"
#include "session.h"
#include "fork.h"
#include "async.h"
#include "header-parser.h"
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
#include <stdio.h>
#include <stdlib.h>

struct cgi {
    struct istream output;

    struct stopwatch *stopwatch;

    struct istream *input;
    struct fifo_buffer *buffer;
    struct strmap *headers;

    /**
     * The remaining number of bytes in the response body, -1 if
     * unknown.
     */
    off_t remaining;

    /**
     * This flag is true while cgi_parse_headers() is calling
     * http_response_handler_invoke_response().  In this case,
     * istream_read(cgi->input) is already up in the stack, and must
     * not be called again.
     */
    bool in_response_callback;

    bool had_input, had_output;

    struct async_operation async;
    struct http_response_handler_ref handler;
};

static bool
cgi_handle_line(struct cgi *cgi, const char *line, size_t length)
{
    assert(cgi != NULL);
    assert(cgi->headers != NULL);
    assert(line != NULL);

    if (length > 0) {
        header_parse_line(cgi->output.pool, cgi->headers,
                          line, length);
        return false;
    } else
        return true;
}

static void
cgi_return_response(struct cgi *cgi)
{
    struct strmap *headers;

    async_operation_finished(&cgi->async);

    headers = cgi->headers;
    cgi->headers = NULL;
    cgi->in_response_callback = true;

    http_status_t status = HTTP_STATUS_OK;
    const char *p = strmap_remove(headers, "status");
    if (p != NULL) {
        int i = atoi(p);
        if (http_status_is_valid(i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status)) {
        /* this response does not have a response body, as indicated
           by the HTTP status code */

        stopwatch_event(cgi->stopwatch, "empty");
        stopwatch_dump(cgi->stopwatch);

        istream_free_handler(&cgi->input);
        http_response_handler_invoke_response(&cgi->handler,
                                              status, headers,
                                              NULL);
        pool_unref(cgi->output.pool);
    } else {
        stopwatch_event(cgi->stopwatch, "headers");

        p = strmap_remove(headers, "content-length");
        if (p != NULL) {
            char *endptr;
            cgi->remaining = (off_t)strtoull(p, &endptr, 10);
            if (endptr == p || *endptr != 0 || cgi->remaining < 0)
                cgi->remaining = -1;
        } else
            cgi->remaining = -1;

        if (cgi->remaining != -1) {
            if ((off_t)fifo_buffer_available(cgi->buffer) > cgi->remaining) {
                istream_free_handler(&cgi->input);

                GError *error =
                    g_error_new_literal(cgi_quark(), 0,
                                        "too much data from CGI script");
                http_response_handler_invoke_abort(&cgi->handler, error);
                cgi->in_response_callback = false;
                pool_unref(cgi->output.pool);
                return;
            }

            cgi->remaining -= fifo_buffer_available(cgi->buffer);
        }

        http_response_handler_invoke_response(&cgi->handler,
                                              status, headers,
                                              istream_struct_cast(&cgi->output));
    }

    cgi->in_response_callback = false;
}

static void
cgi_parse_headers(struct cgi *cgi)
{
    size_t length;
    const char *buffer = fifo_buffer_read(cgi->buffer, &length);
    if (buffer == NULL)
        return;

    assert(length > 0);
    const char *buffer_end = buffer + length;

    bool finished = false;
    const char *start = buffer, *end, *next = NULL;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        finished = cgi_handle_line(cgi, start, end - start + 1);
        if (finished)
            break;

        start = next;
    }

    if (next == NULL)
        return;

    fifo_buffer_consume(cgi->buffer, next - buffer);

    if (finished)
        cgi_return_response(cgi);
}

/*
 * input handler
 *
 */

static size_t
cgi_input_data(const void *data, size_t length, void *ctx)
{
    struct cgi *cgi = ctx;

    cgi->had_input = true;

    if (cgi->headers != NULL) {
        size_t max_length;
        void *dest = fifo_buffer_write(cgi->buffer, &max_length);
        if (dest == NULL)
            return 0;

        if (length > max_length)
            length = max_length;

        memcpy(dest, data, length);
        fifo_buffer_append(cgi->buffer, length);

        pool_ref(cgi->output.pool);

        cgi_parse_headers(cgi);

        /* we check cgi->input here because this is our indicator that
           cgi->output has been closed; since we are in the cgi->input
           data handler, this is the only reason why cgi->input can be
           NULL */
        if (cgi->input == NULL) {
            pool_unref(cgi->output.pool);
            return 0;
        }

        if (cgi->headers == NULL && !fifo_buffer_empty(cgi->buffer)) {
            size_t consumed = istream_buffer_send(&cgi->output, cgi->buffer);
            if (consumed == 0 && cgi->input == NULL)
                length = 0;

            cgi->had_output = true;
        }

        if (cgi->headers == NULL && cgi->input != NULL &&
            cgi->remaining == 0 && fifo_buffer_empty(cgi->buffer)) {
            /* the response body is already finished (probably because
               it was present, but empty); submit that result to the
               handler immediately */

            stopwatch_event(cgi->stopwatch, "end");
            stopwatch_dump(cgi->stopwatch);

            pool_unref(cgi->output.pool);
            istream_close_handler(cgi->input);
            istream_deinit_eof(&cgi->output);
            return 0;
        }

        pool_unref(cgi->output.pool);

        return length;
    } else {
        if (cgi->remaining != -1 && (off_t)length > cgi->remaining) {
            stopwatch_event(cgi->stopwatch, "malformed");
            stopwatch_dump(cgi->stopwatch);

            istream_close_handler(cgi->input);

            GError *error =
                g_error_new_literal(cgi_quark(), 0,
                                    "too much data from CGI script");
            istream_deinit_abort(&cgi->output, error);
            return 0;
        }

        if (cgi->buffer != NULL) {
            size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
            if (rest > 0)
                return 0;

            cgi->buffer = NULL;
        }

        cgi->had_output = true;

        size_t nbytes = istream_invoke_data(&cgi->output, data, length);
        if (nbytes > 0 && cgi->remaining != -1) {
            cgi->remaining -= nbytes;

            if (cgi->remaining == 0) {
                stopwatch_event(cgi->stopwatch, "end");
                stopwatch_dump(cgi->stopwatch);

                istream_close_handler(cgi->input);
                istream_deinit_eof(&cgi->output);
                return 0;
            }
        }

        return nbytes;
    }
}

static ssize_t
cgi_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct cgi *cgi = ctx;

    assert(cgi->headers == NULL);

    cgi->had_input = true;
    cgi->had_output = true;

    if (cgi->remaining == 0) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        istream_close_handler(cgi->input);
        istream_deinit_eof(&cgi->output);
        return ISTREAM_RESULT_CLOSED;
    }

    if (cgi->remaining != -1 && (off_t)max_length > cgi->remaining)
        max_length = (size_t)cgi->remaining;

    ssize_t nbytes = istream_invoke_direct(&cgi->output, type, fd, max_length);
    if (nbytes > 0 && cgi->remaining != -1) {
        cgi->remaining -= nbytes;

        if (cgi->remaining == 0) {
            stopwatch_event(cgi->stopwatch, "end");
            stopwatch_dump(cgi->stopwatch);

            istream_close_handler(cgi->input);
            istream_deinit_eof(&cgi->output);
            return ISTREAM_RESULT_CLOSED;
        }
    }

    return nbytes;
}

static void
cgi_input_eof(void *ctx)
{
    struct cgi *cgi = ctx;

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of headers from CGI script");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else if (cgi->remaining > 0) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of response body from CGI script");
        istream_deinit_abort(&cgi->output, error);
    } else if (cgi->buffer == NULL || fifo_buffer_empty(cgi->buffer)) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        istream_deinit_eof(&cgi->output);
    }
}

static void
cgi_input_abort(GError *error, void *ctx)
{
    struct cgi *cgi = ctx;

    stopwatch_event(cgi->stopwatch, "abort");
    stopwatch_dump(cgi->stopwatch);

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        /* the response hasn't been sent yet: notify the response
           handler */
        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        g_prefix_error(&error, "CGI request body failed: ");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else
        /* response has been sent: abort only the output stream */
        istream_deinit_abort(&cgi->output, error);
}

static const struct istream_handler cgi_input_handler = {
    .data = cgi_input_data,
    .direct = cgi_input_direct,
    .eof = cgi_input_eof,
    .abort = cgi_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct cgi *
istream_to_cgi(struct istream *istream)
{
    return (struct cgi *)(((char*)istream) - offsetof(struct cgi, output));
}

static off_t
istream_cgi_available(struct istream *istream, bool partial)
{
    struct cgi *cgi = istream_to_cgi(istream);

    size_t length;
    if (cgi->buffer != NULL) {
        length = fifo_buffer_available(cgi->buffer);
    } else
        length = 0;

    if (cgi->remaining != -1)
        return (off_t)length + cgi->remaining;

    if (cgi->input == NULL)
        return length;

    if (cgi->in_response_callback) {
        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        if (partial)
            return length;
        else
            return (off_t)-1;
    }

    off_t available = istream_available(cgi->input, partial);
    if (available == (off_t)-1) {
        if (partial)
            return length;
        else
            return (off_t)-1;
    }

    return (off_t)length + available;
}

static void
istream_cgi_read(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL) {
        istream_handler_set_direct(cgi->input, cgi->output.handler_direct);

        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        if (cgi->in_response_callback) {
            return;
        }

        pool_ref(cgi->output.pool);

        cgi->had_output = false;
        do {
            cgi->had_input = false;
            istream_read(cgi->input);
        } while (cgi->input != NULL && cgi->had_input &&
                 !cgi->had_output);

        pool_unref(cgi->output.pool);
    } else {
        size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
        if (rest == 0) {
            stopwatch_event(cgi->stopwatch, "end");
            stopwatch_dump(cgi->stopwatch);

            istream_deinit_eof(&cgi->output);
        }
    }
}

static void
istream_cgi_close(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL)
        istream_free_handler(&cgi->input);

    istream_deinit(&cgi->output);
}

static const struct istream istream_cgi = {
    .available = istream_cgi_available,
    .read = istream_cgi_read,
    .close = istream_cgi_close,
};


/*
 * async operation
 *
 */

static struct cgi *
async_to_cgi(struct async_operation *ao)
{
    return (struct cgi*)(((char*)ao) - offsetof(struct cgi, async));
}

static void
cgi_async_abort(struct async_operation *ao)
{
    struct cgi *cgi = async_to_cgi(ao);

    assert(cgi->input != NULL);

    istream_close_handler(cgi->input);
    pool_unref(cgi->output.pool);
}

static const struct async_operation_class cgi_async_operation = {
    .abort = cgi_async_abort,
};


/*
 * constructor
 *
 */

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

    struct cgi *cgi = (struct cgi *)istream_new(pool, &istream_cgi, sizeof(*cgi));
    cgi->stopwatch = stopwatch;
    istream_assign_handler(&cgi->input, input,
                           &cgi_input_handler, cgi, 0);

    cgi->buffer = fifo_buffer_new(pool, 1024);
    cgi->headers = strmap_new(pool, 32);

    http_response_handler_set(&cgi->handler, handler, handler_ctx);

    async_init(&cgi->async, &cgi_async_operation);
    async_ref_set(async_ref, &cgi->async);

    istream_read(input);
}
