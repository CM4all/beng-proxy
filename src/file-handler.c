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
#include "widget.h"
#include "embed.h"
#include "frame.h"
#include "http-util.h"
#include "proxy-widget.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

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
#ifndef NO_XATTR
    ssize_t nbytes;
    char content_type[256];
#endif
    char buffer[64];

    (void)connection;

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        request->method != HTTP_METHOD_POST) {
        http_server_send_message(request,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not allowed.");
        return;
    }

    ret = lstat(translated->path, &st);
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

#ifndef NO_XATTR
    nbytes = getxattr(translated->path, "user.Content-Type", /* XXX use fgetxattr() */
                      content_type, sizeof(content_type) - 1);
    if (nbytes > 0) {
        assert((size_t)nbytes < sizeof(content_type));
        content_type[nbytes] = 0;
        header_write(headers, "content-type", content_type);
    } else {
        content_type[0] = 0;
#endif /* #ifndef NO_XATTR */
        header_write(headers, "content-type", "application/octet-stream");
#ifndef NO_XATTR
    }

    if (strncmp(content_type, "text/html", 9) == 0) {
        if (body != NULL) {
            struct processor_env *env;
            struct widget *widget;
            unsigned processor_options = 0;

            env = p_malloc(request->pool, sizeof(*env));
            processor_env_init(request->pool, env, &translated->uri,
                               request->content_length, request->body,
                               embed_widget_callback);
            if (env->frame != NULL) { /* XXX */
                env->widget_callback = frame_widget_callback;

                /* do not show the template contents if the browser is
                   only interested in one particular widget for
                   displaying the frame */
                processor_options |= PROCESSOR_QUIET;
            }

            widget = p_malloc(request->pool, sizeof(*widget));
            widget_init(widget, NULL);

            body = processor_new(request->pool, body, widget, env,
                                 processor_options);

            if (env->frame != NULL) {
                /* XXX */
                widget_proxy_install(env, request, body);
                return;
            }

#ifndef NO_DEFLATE
            if (http_client_accepts_encoding(request->headers, "deflate")) {
                header_write(headers, "content-encoding", "deflate");
                body = istream_deflate_new(request->pool, body);
            }
#endif
        }

        http_server_response(request, HTTP_STATUS_OK, headers,
                             (off_t)-1, body);
    } else {
#endif /* #ifndef NO_XATTR */
        if (request->method == HTTP_METHOD_POST) {
            istream_close(body);
            http_server_send_message(request,
                                     HTTP_STATUS_METHOD_NOT_ALLOWED,
                                     "This method is not allowed.");
            return;
        }

        if (request->body != NULL)
            istream_close(request->body);

#ifndef NO_LAST_MODIFIED_HEADER
        header_write(headers, "last-modified", http_date_format(st.st_mtime));
#endif

        http_server_response(request, HTTP_STATUS_OK, headers, st.st_size, body);
#ifndef NO_XATTR
    }
#endif
}
