/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "connection.h"
#include "header-writer.h"
#include "processor.h"
#include "date.h"
#include "format.h"
#include "http-util.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

static bool
parse_range_header(const char *p, off_t *skip_r, off_t *size_r)
{
    unsigned long v;
    char *endptr;

    assert(p != NULL);
    assert(skip_r != NULL);
    assert(size_r != NULL);

    if (memcmp(p, "bytes=", 6) != 0)
        return false;

    p += 6;

    if (*p == '-') {
        ++p;

        v = strtoul(p, &endptr, 10);
        if (v > (unsigned long)*size_r)
            return false;

        *skip_r = *size_r - v;
    } else {
        *skip_r = strtoul(p, &endptr, 10);
        if (*skip_r > *size_r)
            return false;

        if (*endptr == '-') {
            v = strtoul(endptr + 1, NULL, 10);
            if (v < (unsigned long)*skip_r)
                return false;

            if (v < (unsigned long)*size_r)
                *size_r = v + 1;
        }
    }

    return true;
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

void
file_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *path;
    int ret;
    growing_buffer_t headers;
    istream_t body;
    struct stat st;
    bool range = false;
    off_t skip, size;
    char buffer[64];
    http_status_t status;

    assert(tr != NULL);
    assert(tr->address.u.path != NULL);

    path = tr->address.u.path;

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        (!request_processor_enabled(request2) ||
         request->method != HTTP_METHOD_POST)) {
        http_server_send_message(request,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not allowed.");
        return;
    }

    /* get file information */

    ret = lstat(path, &st);
    if (ret != 0) {
        if (errno == ENOENT) {
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Not a regular file");
        return;
    }

    size = st.st_size;

    /* request options */

    if (!request_transformation_enabled(request2)) {
        const char *p = strmap_get(request->headers, "if-modified-since");
        if (p != NULL) {
            time_t t = http_date_parse(p);
            if (t != (time_t)-1 && st.st_mtime > t) {
                http_server_response(request,
                                     HTTP_STATUS_NOT_MODIFIED,
                                     NULL, NULL);
                return;
            }
        }

        p = strmap_get(request->headers, "if-unmodified-since");
        if (p != NULL) {
            time_t t = http_date_parse(p);
            if (t != (time_t)-1 && st.st_mtime < t) {
                http_server_response(request,
                                     HTTP_STATUS_PRECONDITION_FAILED,
                                     NULL, NULL);
                return;
            }
        }

        p = strmap_get(request->headers, "if-match");
        if (p != NULL && strcmp(p, "*") != 0) {
            make_etag(buffer, &st);

            if (!http_list_contains(p, buffer)) {
                http_server_response(request,
                                     HTTP_STATUS_PRECONDITION_FAILED,
                                     NULL, NULL);
                return;
            }
        }

        p = strmap_get(request->headers, "if-none-match");
        if (p != NULL && strcmp(p, "*") == 0) {
            http_server_response(request,
                                 HTTP_STATUS_PRECONDITION_FAILED,
                                 NULL, NULL);
            return;
        }

        if (p != NULL) {
            make_etag(buffer, &st);

            if (http_list_contains(p, buffer)) {
                http_server_response(request,
                                     HTTP_STATUS_PRECONDITION_FAILED,
                                     NULL, NULL);
                return;
            }
        }
    }

    if (tr->status == 0 && request->method == HTTP_METHOD_GET &&
        !request_transformation_enabled(request2)) {
        const char *p = strmap_get(request->headers, "range");

        if (p != NULL)
            range = parse_range_header(p, &skip, &size);
    }

    /* build the response */

    body = istream_file_new(request->pool, path, size);
    if (body == NULL) {
        if (errno == ENOENT) {
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    headers = growing_buffer_new(request->pool, 2048);

    status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    if (range) {
        istream_skip(body, skip);

        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers, "content-range",
                     p_sprintf(request->pool, "bytes %lu-%lu/%lu",
                               (unsigned long)skip,
                               (unsigned long)(size - 1),
                               (unsigned long)st.st_size));
    } else if (tr->status == 0 && !request_transformation_enabled(request2)) {
        header_write(headers, "accept-ranges", "bytes");
    }

    if (!request_transformation_enabled(request2)) {
        make_etag(buffer, &st);
        header_write(headers, "etag", buffer);
    }

    if (tr->content_type != NULL) {
        /* content type override from the translation server */
        header_write(headers, "content-type", tr->content_type);
    } else {
#ifndef NO_XATTR
        ssize_t nbytes;
        char content_type[256];

        nbytes = getxattr(path, "user.Content-Type", /* XXX use fgetxattr() */
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

    if (!request_processor_enabled(request2)) {
#ifndef NO_LAST_MODIFIED_HEADER
        header_write(headers, "last-modified", http_date_format(st.st_mtime));
#endif

        if (request->body != NULL)
            istream_close(request->body);
    }

    /* finished, dispatch this response */

    response_dispatch(request2, status, headers, body);
}
