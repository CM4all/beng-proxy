/*
 * Serve HTTP requests from delegate helpers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "Address.hxx"
#include "Glue.hxx"
#include "file_handler.hxx"
#include "file_headers.hxx"
#include "file_address.hxx"
#include "generate_response.hxx"
#include "bp_instance.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "http_response.hxx"
#include "istream/istream_file.hxx"

#include <glib.h>

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * delegate_handler
 *
 */

void
Request::OnDelegateSuccess(int fd)
{
    /* get file information */

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);

        response_dispatch_message(*this, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);

        response_dispatch_message(*this, HTTP_STATUS_NOT_FOUND,
                                  "Not a regular file");
        return;
    }

    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
        .size = st.st_size,
    };

    /* request options */

    if (!file_evaluate_request(*this, fd, st, file_request)) {
        close(fd);
        return;
    }

    /* build the response */

    file_dispatch(*this, st, file_request,
                  istream_file_fd_new(instance.event_loop, pool,
                                      handler.delegate.path,
                                      fd, FdType::FD_FILE, file_request.size));
}

void
Request::OnDelegateError(GError *error)
{
    response_dispatch_error(*this, error);
    g_error_free(error);
}

/*
 * public
 *
 */

void
delegate_handler(Request &request2, const DelegateAddress &address,
                 const char *path)
{
    auto &request = request2.request;

    assert(path != nullptr);

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    request2.handler.delegate.path = path;

    delegate_stock_open(request2.instance.delegate_stock, &request.pool,
                        address.delegate, address.child_options,
                        path,
                        request2, request2.async_ref);
}
