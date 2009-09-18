/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file-handler.h"
#include "request.h"
#include "connection.h"
#include "header-writer.h"
#include "processor.h"
#include "date.h"
#include "format.h"
#include "http-util.h"
#include "growing-buffer.h"
#include "http-server.h"
#include "delegate-get.h"
#include "global.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

static enum range_type
parse_range_header(const char *p, off_t *skip_r, off_t *size_r)
{
    unsigned long v;
    char *endptr;

    assert(p != NULL);
    assert(skip_r != NULL);
    assert(size_r != NULL);

    if (memcmp(p, "bytes=", 6) != 0)
        return RANGE_INVALID;

    p += 6;

    if (*p == '-') {
        /* suffix-byte-range-spec */
        ++p;

        v = strtoul(p, &endptr, 10);
        if (v >= (unsigned long)*size_r)
            return RANGE_NONE;

        *size_r = v;
    } else {
        *skip_r = strtoul(p, &endptr, 10);
        if (*skip_r >= *size_r)
            return RANGE_INVALID;

        if (*endptr == '-') {
            p = endptr + 1;
            if (*p == 0)
                /* "wget -c" */
                return RANGE_VALID;

            v = strtoul(p, &endptr, 10);
            if (*endptr != 0 || v < (unsigned long)*skip_r ||
                v >= (unsigned long)*size_r)
                return RANGE_INVALID;

            *size_r = v + 1;
        }
    }

    return RANGE_VALID;
}

static void
make_etag(char *p, const struct stat *st)
{
    *p++ = '"';

    p += format_uint32_hex(p, (uint32_t)st->st_dev);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_ino);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_mtime);

    *p++ = '"';
    *p = 0;
}

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

bool
file_evaluate_request(struct request *request2, const struct stat *st,
                      struct file_request *file_request)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *p;
    char buffer[64];

    if (request_transformation_enabled(request2))
        return true;

    if (tr->status == 0 && request->method == HTTP_METHOD_GET &&
        !request_transformation_enabled(request2)) {
        p = strmap_get(request->headers, "range");

        if (p != NULL)
            file_request->range =
                parse_range_header(p, &file_request->skip,
                                   &file_request->size);
    }

    p = strmap_get(request->headers, "if-modified-since");
    if (p != NULL) {
        time_t t = http_date_parse(p);
        if (t != (time_t)-1 && st->st_mtime <= t) {
            request_discard_body(request2);
            http_server_response(request,
                                 HTTP_STATUS_NOT_MODIFIED,
                                 NULL, NULL);
            return false;
        }
    }

    p = strmap_get(request->headers, "if-unmodified-since");
    if (p != NULL) {
        time_t t = http_date_parse(p);
        if (t != (time_t)-1 && st->st_mtime > t) {
            request_discard_body(request2);
            http_server_response(request,
                                 HTTP_STATUS_PRECONDITION_FAILED,
                                 NULL, NULL);
            return false;
        }
    }

    p = strmap_get(request->headers, "if-match");
    if (p != NULL && strcmp(p, "*") != 0) {
        make_etag(buffer, st);

        if (!http_list_contains(p, buffer)) {
            request_discard_body(request2);
            http_server_response(request,
                                 HTTP_STATUS_PRECONDITION_FAILED,
                                 NULL, NULL);
            return false;
        }
    }

    p = strmap_get(request->headers, "if-none-match");
    if (p != NULL && strcmp(p, "*") == 0) {
        request_discard_body(request2);
        http_server_response(request,
                             HTTP_STATUS_PRECONDITION_FAILED,
                             NULL, NULL);
        return false;
    }

    if (p != NULL) {
        make_etag(buffer, st);

        if (http_list_contains(p, buffer)) {
            request_discard_body(request2);
            http_server_response(request,
                                 HTTP_STATUS_PRECONDITION_FAILED,
                                 NULL, NULL);
            return false;
        }
    }

    return true;
}

void
file_dispatch(struct request *request2, const struct stat *st,
              const struct file_request *file_request,
              istream_t body)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    struct growing_buffer *headers;
    http_status_t status;
    char buffer[64];

    headers = growing_buffer_new(request->pool, 2048);

    status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    if (!request_processor_first(request2)) {
#ifndef NO_XATTR
        ssize_t nbytes;
        char etag[512];

        nbytes = fgetxattr(istream_file_fd(body), "user.ETag",
                           etag + 1, sizeof(etag) - 3);
        if (nbytes > 0) {
            assert((size_t)nbytes < sizeof(etag));
            etag[0] = '"';
            etag[nbytes + 1] = '"';
            etag[nbytes + 2] = 0;
            header_write(headers, "etag", etag);
        } else {
#endif
            make_etag(buffer, st);
            header_write(headers, "etag", buffer);
#ifndef NO_XATTR
        }
#endif

#ifndef NO_XATTR
        nbytes = fgetxattr(istream_file_fd(body), "user.MaxAge",
                           buffer, sizeof(buffer) - 1);
        if (nbytes > 0) {
            char *endptr;
            long max_age;

            buffer[nbytes] = 0;
            max_age = strtol(buffer, &endptr, 10);

            if (*endptr == 0 && max_age > 0) {
                if (max_age > 365 * 24 * 3600)
                    /* limit max_age to approximately one year */
                    max_age = 365 * 24 * 3600;

                /* generate an "Expires" response header */
                header_write(headers, "expires",
                             http_date_format(time(NULL) + max_age));
            }
        }
#endif
    }

    if (tr->address.u.local.content_type != NULL) {
        /* content type override from the translation server */
        header_write(headers, "content-type", tr->address.u.local.content_type);
    } else {
#ifndef NO_XATTR
        ssize_t nbytes;
        char content_type[256];

        nbytes = fgetxattr(istream_file_fd(body), "user.Content-Type",
                           content_type, sizeof(content_type) - 1);
        if (nbytes > 0) {
            assert((size_t)nbytes < sizeof(content_type));
            content_type[nbytes] = 0;
            header_write(headers, "content-type", content_type);
        } else {
#endif /* #ifndef NO_XATTR */
            header_write(headers, "content-type", "application/octet-stream");
#ifndef NO_XATTR
        }
#endif /* #ifndef NO_XATTR */
    }

#ifndef NO_LAST_MODIFIED_HEADER
    if (!request_processor_enabled(request2))
        header_write(headers, "last-modified", http_date_format(st->st_mtime));
#endif

    /* generate the Content-Range header */

    header_write(headers, "accept-ranges", "bytes");

    switch (file_request->range) {
    case RANGE_NONE:
        break;

    case RANGE_VALID:
        istream_skip(body, file_request->skip);

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

        istream_free(&body);
        break;
    }

    /* finished, dispatch this response */

    response_dispatch(request2, status, headers, body);
}

void
file_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *path;
    int ret;
    istream_t body;
    struct stat st;
    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
    };

    assert(tr != NULL);
    assert(tr->address.u.local.path != NULL);

    path = tr->address.u.local.path;

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        !request2->processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* delegate? */

    if (tr->address.u.local.delegate != NULL) {
        delegate_stock_get(global_delegate_stock, request->pool,
                           tr->address.u.local.delegate, path,
                           tr->address.u.local.content_type,
                           &response_handler, request2,
                           request2->async_ref);
        return;
    }

    /* get file information */

    ret = lstat(path, &st);
    if (ret != 0) {
        if (errno == ENOENT) {
            request_discard_body(request2);
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            request_discard_body(request2);
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        request_discard_body(request2);
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(request2, &st, &file_request))
        return;

    /* build the response */

    body = istream_file_new(request->pool, path, file_request.size);
    if (body == NULL) {
        if (errno == ENOENT) {
            request_discard_body(request2);
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            request_discard_body(request2);
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    file_dispatch(request2, &st, &file_request, body);
}
