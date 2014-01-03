/*
 * Serve HTTP requests from delegate helpers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"

extern "C" {
#include "file_handler.h"
#include "file_headers.h"
#include "static-headers.h"
#include "delegate_glue.h"
#include "http_error.h"
#include "generate_response.h"
#include "request.h"
#include "http_server.h"
#include "http_response.h"
#include "global.h"
#include "istream_file.h"
}

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * delegate_handler
 *
 */

static void
delegate_handler_callback(int fd, void *ctx)
{
    request &request2 = *(request *)ctx;
    struct http_server_request *request = request2.request;
    const struct translate_response *tr = request2.translate.response;
    int ret;
    struct stat st;
    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
        .size = 0,
    };

    /* get file information */

    ret = fstat(fd, &st);
    if (ret < 0) {
        close(fd);

        response_dispatch_message(&request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);

        response_dispatch_message(&request2, HTTP_STATUS_NOT_FOUND,
                                  "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(&request2, fd, &st, &file_request)) {
        close(fd);
        return;
    }

    /* build the response */

    file_dispatch(&request2, &st, &file_request,
                  istream_file_fd_new(request->pool,
                                      tr->address.u.file->path,
                                      fd, ISTREAM_FILE, file_request.size));
}

static void
delegate_handler_error(GError *error, void *ctx)
{
    request &request2 = *(request *)ctx;

    response_dispatch_error(&request2, error);
    g_error_free(error);
}

static const struct delegate_handler delegate_handler_handler = {
    .success = delegate_handler_callback,
    .error = delegate_handler_error,
};

/*
 * public
 *
 */

void
delegate_handler(request &request2)
{
    struct http_server_request *request = request2.request;
    const struct translate_response *tr = request2.translate.response;

    assert(tr != NULL);
    assert(tr->address.u.file->path != NULL);
    assert(tr->address.u.file->delegate != NULL);

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(&request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    delegate_stock_open(global_delegate_stock, request->pool,
                        tr->address.u.file->delegate,
                        &tr->address.u.file->child_options,
                        tr->address.u.file->path,
                        &delegate_handler_handler, &request2,
                        &request2.async_ref);
}
