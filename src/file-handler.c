/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"
#include "header-writer.h"
#include "processor.h"
#include "date.h"
#include "format.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <attr/xattr.h>
#include <string.h>
#include <stdio.h>

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
file_callback(struct client_connection *connection,
              struct http_server_request *request,
              struct translated *translated)
{
    int ret;
    growing_buffer_t headers;
    istream_t body;
    struct stat st;
    ssize_t nbytes;
    char buffer[64], content_type[256];

    (void)connection;

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        request->method != HTTP_METHOD_POST) {
        http_server_send_message(request,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not allowed.");
        return;
    }

    ret = stat(translated->path, &st);
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

    if (request->method != HTTP_METHOD_HEAD) {
        body = istream_file_new(request->pool, translated->path, st.st_size);
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
    } else {
        body = NULL;
    }

    headers = growing_buffer_new(request->pool, 2048);

    make_etag(buffer, &st);
    header_write(headers, "etag", buffer);

    nbytes = getxattr(translated->path, "user.Content-Type", /* XXX use fgetxattr() */
                      content_type, sizeof(content_type) - 1);
    if (nbytes > 0) {
        assert((size_t)nbytes < sizeof(content_type));
        content_type[nbytes] = 0;
        header_write(headers, "content-type", content_type);
    } else {
        content_type[0] = 0;
        header_write(headers, "content-type", "application/octet-stream");
    }

    if (strncmp(content_type, "text/html", 9) == 0) {
        if (body != NULL) {
            struct processor_env *env;

            env = p_malloc(request->pool, sizeof(*env));
            processor_env_init(request->pool, env, &translated->uri);

            body = processor_new(request->pool, body, NULL, env);
        }

        if (request->body != NULL)
            istream_close(request->body); /* XXX */

        http_server_response(request, HTTP_STATUS_OK, headers,
                             (off_t)-1, body);
    } else {
        if (request->method == HTTP_METHOD_POST) {
            istream_close(body);
            http_server_send_message(request,
                                     HTTP_STATUS_METHOD_NOT_ALLOWED,
                                     "This method is not allowed.");
            return;
        }

        if (request->body != NULL)
            istream_close(request->body);

        header_write(headers, "last-modified", http_date_format(st.st_mtime));

        http_server_response(request, HTTP_STATUS_OK, headers, st.st_size, body);
    }
}
