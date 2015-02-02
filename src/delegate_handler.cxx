/*
 * Serve HTTP requests from delegate helpers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "file_handler.hxx"
#include "file_headers.hxx"
#include "file_address.hxx"
#include "delegate_glue.hxx"
#include "delegate_client.hxx"
#include "generate_response.hxx"
#include "request.hxx"
#include "http_server.hxx"
#include "http_response.hxx"
#include "bp_global.hxx"
#include "istream_file.hxx"

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
    struct http_server_request &request = *request2.request;
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

        response_dispatch_message(request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);

        response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                  "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(request2, fd, &st, &file_request)) {
        close(fd);
        return;
    }

    /* build the response */

    const struct file_address &address = *request2.translate.address->u.file;

    file_dispatch(request2, st, file_request,
                  istream_file_fd_new(request.pool,
                                      address.path,
                                      fd, ISTREAM_FILE, file_request.size));
}

static void
delegate_handler_error(GError *error, void *ctx)
{
    request &request2 = *(request *)ctx;

    response_dispatch_error(request2, error);
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
    struct http_server_request &request = *request2.request;
    const struct file_address &address = *request2.translate.address->u.file;

    assert(address.path != NULL);
    assert(address.delegate != NULL);

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    delegate_stock_open(global_delegate_stock, request.pool,
                        address.delegate,
                        &address.child_options,
                        address.path,
                        &delegate_handler_handler, &request2,
                        &request2.async_ref);
}
