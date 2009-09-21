/*
 * Serve HTTP requests from delegate helpers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "file-handler.h"
#include "delegate-glue.h"
#include "http-error.h"
#include "request.h"
#include "header-writer.h"
#include "growing-buffer.h"
#include "http-server.h"
#include "global.h"

#include <assert.h>
#include <sys/stat.h>

static void
method_not_allowed(struct request *request2, const char *allow)
{
    struct http_server_request *request = request2->request;
    struct growing_buffer *headers = growing_buffer_new(request->pool, 128);

    assert(allow != NULL);

    header_write(headers, "content-type", "text/plain");
    header_write(headers, "allow", allow);

    request_discard_body(request2);
    http_server_response(request, HTTP_STATUS_METHOD_NOT_ALLOWED, headers,
                         istream_string_new(request->pool,
                                            "This method is not allowed."));
}

static void
delegate_handler_callback(int fd, void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    int ret;
    struct stat st;
    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
    };

    if (fd < 0) {
        struct http_response_handler_ref handler_ref;
        request_discard_body(request2);
        http_response_handler_set(&handler_ref, &response_handler, request2);
        http_response_handler_invoke_errno(&handler_ref, request->pool, -fd);
        return;
    }

    /* get file information */

    ret = fstat(fd, &st);
    if (ret < 0) {
        close(fd);

        request_discard_body(request2);
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);

        request_discard_body(request2);
        http_server_send_message(request, HTTP_STATUS_NOT_FOUND,
                                 "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(request2, &st, &file_request)) {
        close(fd);
        return;
    }

    /* build the response */

    file_dispatch(request2, &st, &file_request,
                  istream_file_fd_new(request->pool,
                                      tr->address.u.local.path,
                                      fd, file_request.size));
}

void
delegate_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;

    assert(tr != NULL);
    assert(tr->address.u.local.path != NULL);
    assert(tr->address.u.local.delegate != NULL);

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        !request2->processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    delegate_stock_open(global_delegate_stock, request->pool,
                        tr->address.u.local.delegate,
                        tr->address.u.local.path,
                        delegate_handler_callback, request2,
                        request2->async_ref);
}
