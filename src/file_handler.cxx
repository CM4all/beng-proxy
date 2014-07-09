/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_handler.hxx"
#include "file_headers.hxx"
#include "file_address.hxx"
#include "request.hxx"
#include "generate_response.hxx"
#include "header_writer.hxx"
#include "date.h"
#include "format.h"
#include "http_util.hxx"
#include "growing_buffer.hxx"
#include "http_server.hxx"
#include "global.h"
#include "istream_file.h"
#include "istream.h"
#include "tvary.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#ifndef HAVE_ATTR_XATTR_H
#define NO_XATTR 1
#endif

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

void
file_dispatch(struct request *request2, const struct stat *st,
              const struct file_request *file_request,
              struct istream *body)
{
    struct http_server_request *request = request2->request;
    const TranslateResponse *tr = request2->translate.response;
    const struct file_address &address = *request2->translate.address->u.file;
    struct growing_buffer *headers;
    http_status_t status;

    const char *override_content_type = request2->translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    headers = growing_buffer_new(request->pool, 2048);
    file_response_headers(headers, override_content_type,
                          istream_file_fd(body), st,
                          tr->expires_relative,
                          request_processor_enabled(request2),
                          request_processor_first(request2));
    write_translation_vary_header(headers, request2->translate.response);

    status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    /* generate the Content-Range header */

    header_write(headers, "accept-ranges", "bytes");

    switch (file_request->range) {
    case RANGE_NONE:
        break;

    case RANGE_VALID:
        istream_file_set_range(body, file_request->skip,
                               file_request->size);

        assert(istream_available(body, false) ==
               file_request->size - file_request->skip);

        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers, "content-range",
                     p_sprintf(request->pool, "bytes %lu-%lu/%lu",
                               (unsigned long)file_request->skip,
                               (unsigned long)(file_request->size - 1),
                               (unsigned long)st->st_size));
        break;

    case RANGE_INVALID:
        status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        header_write(headers, "content-range",
                     p_sprintf(request->pool, "bytes */%lu",
                               (unsigned long)st->st_size));

        istream_free_unused(&body);
        break;
    }

    /* finished, dispatch this response */

    response_dispatch(request2, status, headers, body);
}

static bool
file_dispatch_compressed(struct request *request2, const struct stat *st,
                         struct istream *body, const char *encoding,
                         const char *path)
{
    struct http_server_request *request = request2->request;
    const TranslateResponse *tr = request2->translate.response;
    const struct file_address &address = *request2->translate.address->u.file;
    const char *accept_encoding;
    struct growing_buffer *headers;
    http_status_t status;
    struct istream *compressed_body;
    struct stat st2;

    accept_encoding = strmap_get(request->headers, "accept-encoding");
    if (accept_encoding == nullptr ||
        !http_list_contains(accept_encoding, encoding))
        /* encoding not supported by the client */
        return false;

    /* open compressed file */

    compressed_body = istream_file_stat_new(request->pool, path, &st2, NULL);
    if (compressed_body == nullptr)
        return false;

    if (!S_ISREG(st2.st_mode)) {
        istream_close_unused(compressed_body);
        return false;
    }

    /* response headers with information from uncompressed file */

    const char *override_content_type = request2->translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    headers = growing_buffer_new(request->pool, 2048);
    file_response_headers(headers, override_content_type,
                          istream_file_fd(body), st,
                          tr->expires_relative,
                          request_processor_enabled(request2),
                          request_processor_first(request2));
    write_translation_vary_header(headers, request2->translate.response);

    header_write(headers, "content-encoding", encoding);
    header_write(headers, "vary", "accept-encoding");

    /* close original file */

    istream_close_unused(body);

    /* finished, dispatch this response */

    status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;
    response_dispatch(request2, status, headers, compressed_body);
    return true;
}

void
file_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const TranslateResponse *tr = request2->translate.response;
    const struct file_address &address = *request2->translate.address->u.file;
    const char *path;
    struct istream *body;
    struct stat st;
    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
    };

    assert(tr != nullptr);
    assert(address.path != nullptr);
    assert(address.delegate == nullptr);

    path = address.path;

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        !request2->processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* open the file */

    GError *error = nullptr;
    body = istream_file_stat_new(request->pool, path, &st, &error);
    if (body == nullptr) {
        response_dispatch_error(request2, error);
        g_error_free(error);
        return;
    }

    /* check file type */

    if (S_ISCHR(st.st_mode)) {
        /* allow character devices, but skip range etc. */
        response_dispatch(request2, HTTP_STATUS_OK, nullptr, body);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(request2, istream_file_fd(body), &st,
                               &file_request)) {
        istream_close_unused(body);
        return;
    }

    /* precompressed? */

    if (file_request.range == RANGE_NONE &&
        !request_transformation_enabled(request2) &&
        ((address.deflated != nullptr &&
          file_dispatch_compressed(request2, &st, body, "deflate",
                                   address.deflated)) ||
         (address.gzipped != nullptr &&
          file_dispatch_compressed(request2, &st, body, "gzip",
                                   address.gzipped))))
        return;

    /* build the response */

    file_dispatch(request2, &st, &file_request, body);
}
