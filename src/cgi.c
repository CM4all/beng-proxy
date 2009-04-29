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

    istream_t input;
    struct fifo_buffer *buffer;
    struct strmap *headers;

    /**
     * This flag is true while cgi_parse_headers() is calling
     * http_response_handler_invoke_response().  In this case,
     * istream_read(cgi->input) is already up in the stack, and must
     * not be called again.
     */
    bool in_response_callback;

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
cgi_parse_headers(struct cgi *cgi)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;
    int ret = 0;

    buffer = fifo_buffer_read(cgi->buffer, &length);
    if (buffer == NULL)
        return;

    assert(length > 0);
    buffer_end = buffer + length;

    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        if (likely(*end == '\r'))
            --end;
        while (unlikely(end >= start && char_is_whitespace(*end)))
            --end;

        ret = cgi_handle_line(cgi, start, end - start + 1);
        if (ret)
            break;

        start = next;
    }

    if (next == NULL)
        return;

    fifo_buffer_consume(cgi->buffer, next - buffer);

    if (ret) {
        struct strmap *headers;

        async_poison(&cgi->async);

        headers = cgi->headers;
        cgi->headers = NULL;
        cgi->in_response_callback = true;

        http_response_handler_invoke_response(&cgi->handler,
                                              HTTP_STATUS_OK, headers,
                                              istream_struct_cast(&cgi->output));

        cgi->in_response_callback = false;
    }
}

/*
 * input handler
 *
 */

static size_t
cgi_input_data(const void *data, size_t length, void *ctx)
{
    struct cgi *cgi = ctx;

    if (cgi->headers != NULL) {
        void *dest;
        size_t max_length;

        dest = fifo_buffer_write(cgi->buffer, &max_length);
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

        if (cgi->headers == NULL) {
            size_t consumed = istream_buffer_send(&cgi->output, cgi->buffer);
            if (consumed == 0 && cgi->input == NULL)
                length = 0;
        }

        pool_unref(cgi->output.pool);

        return length;
    } else {
        if (cgi->buffer != NULL) {
            size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
            if (rest > 0)
                return 0;

            cgi->buffer = NULL;
        }

        return istream_invoke_data(&cgi->output, data, length);
    }
}

static ssize_t
cgi_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct cgi *cgi = ctx;

    assert(cgi->headers == NULL);

    return istream_invoke_direct(&cgi->output, type, fd, max_length);
}

static void
cgi_input_eof(void *ctx)
{
    struct cgi *cgi = ctx;

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        daemon_log(1, "premature end of headers from CGI script\n");

        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        http_response_handler_invoke_abort(&cgi->handler);
        pool_unref(cgi->output.pool);
    } else if (cgi->buffer == NULL || fifo_buffer_empty(cgi->buffer)) {
        istream_deinit_eof(&cgi->output);
    }
}

static void
cgi_input_abort(void *ctx)
{
    struct cgi *cgi = ctx;

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        /* the response hasn't been sent yet: notify the response
           handler */
        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        http_response_handler_invoke_abort(&cgi->handler);
        pool_unref(cgi->output.pool);
    } else
        /* response has been sent: abort only the output stream */
        istream_deinit_abort(&cgi->output);
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
istream_to_cgi(istream_t istream)
{
    return (struct cgi *)(((char*)istream) - offsetof(struct cgi, output));
}

static off_t
istream_cgi_available(istream_t istream, bool partial)
{
    struct cgi *cgi = istream_to_cgi(istream);
    const void *data;
    size_t length;
    off_t available;

    if (cgi->buffer != NULL) {
        data = fifo_buffer_read(cgi->buffer, &length);
        if (data == NULL)
            length = 0;
    } else
        length = 0;

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

    available = istream_available(cgi->input, partial);
    if (available == (off_t)-1) {
        if (partial)
            return length;
        else
            return (off_t)-1;
    }

    return (off_t)length + available;
}

static void
istream_cgi_read(istream_t istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL) {
        istream_handler_set_direct(cgi->input, cgi->output.handler_direct);

        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        if (!cgi->in_response_callback)
            istream_read(cgi->input);
    } else {
        size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
        if (rest == 0)
            istream_deinit_eof(&cgi->output);
    }
}

static void
istream_cgi_close(istream_t istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL)
        istream_close(cgi->input);
    else
        istream_deinit_abort(&cgi->output);
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

static void __attr_noreturn
cgi_run(bool jail, const char *interpreter, const char *action,
        const char *path,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        struct strmap *headers)
{
    const struct strmap_pair *pair;
    char buffer[512] = "HTTP_";
    size_t i;
    const char *arg = NULL;

    assert(path != NULL);
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

    if (jail) {
        setenv("JAILCGI_FILENAME", path, 1);
        path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

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

    if (headers != NULL) {
        strmap_rewind(headers);
        while ((pair = strmap_next(headers)) != NULL) {
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

    execl(path, path, arg, NULL);
    fprintf(stderr, "exec('%s') failed: %s\n",
            path, strerror(errno));
    _exit(2);
}

static void
cgi_child_callback(int status, void *ctx __attr_unused)
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
cgi_new(pool_t pool, bool jail,
        const char *interpreter, const char *action,
        const char *path,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        struct strmap *headers, istream_t body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref)
{
    struct cgi *cgi;
    pid_t pid;
    istream_t input;

    pid = beng_fork(pool, body, &input,
                    cgi_child_callback, NULL);
    if (pid < 0) {
        if (body != NULL) {
            /* beng_fork() left the request body open - free this
               resource, because our caller always assume that we have
               consumed it */
            struct abort_flag abort_flag;
            abort_flag_set(&abort_flag, async_ref);

            istream_close(body);

            if (abort_flag.aborted)
                /* the operation was aborted - don't call the
                   http_response_handler */
                return;
        }

        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    if (pid == 0)
        cgi_run(jail, interpreter, action, path, method, uri,
                script_name, path_info, query_string, document_root,
                headers);

    cgi = (struct cgi *)istream_new(pool, &istream_cgi, sizeof(*cgi));
    istream_assign_handler(&cgi->input, input,
                           &cgi_input_handler, cgi, 0);

    cgi->buffer = fifo_buffer_new(pool, 1024);
    cgi->headers = strmap_new(pool, 32);

    http_response_handler_set(&cgi->handler, handler, handler_ctx);

    async_init(&cgi->async, &cgi_async_operation);
    async_ref_set(async_ref, &cgi->async);

    istream_read(input);
}
