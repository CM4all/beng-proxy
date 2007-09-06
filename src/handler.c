/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct translated *
translate(struct http_server_request *request)
{
    struct translated *translated;
    char path[1024];

    translated = p_malloc(request->pool, sizeof(*translated));

    uri_parse(&translated->uri, request->uri);

    if (memcmp(request->uri, "/proxy/", 7) == 0) {
        /* XXX append query string */
        snprintf(path, sizeof(path), "http://dory.intern.cm-ag/~max/%.*s",
                 (int)translated->uri.base_length - 7, translated->uri.base + 7);
    } else if (memcmp(request->uri, "/test/", 6) == 0) {
        /* XXX append query string */
        snprintf(path, sizeof(path), "http://cfatest01.intern.cm-ag/%.*s",
                 (int)translated->uri.base_length - 6, translated->uri.base + 6);
    } else {
        /* XXX this is, of course, a huge security hole */
        snprintf(path, sizeof(path), "/var/www/%.*s",
                 (int)translated->uri.base_length, translated->uri.base);
    }

    translated->path = p_strdup(request->pool, path);
    return translated;
}

static void
my_http_server_connection_request(struct http_server_request *request,
                                  void *ctx)
{
    struct client_connection *connection = ctx;
    struct translated *translated;

    assert(request != NULL);

    (void)request;
    (void)connection;

    translated = translate(request);
    if (translated == NULL) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (translated == NULL || translated->path == NULL) {
        http_server_send_message(request,
                                 HTTP_STATUS_NOT_FOUND,
                                 "The requested resource does not exist.");
        return;
    }

    if (memcmp(translated->path, "http://", 7) == 0)
        proxy_callback(connection, request, translated);
    else
        file_callback(connection, request, translated);
}

static void
my_http_server_connection_free(void *ctx)
{
    struct client_connection *connection = ctx;

    /* since remove_connection() might recurse here, we check if
       the connection has already been removed from the linked
       list */
    if (connection->http != NULL)
        remove_connection(connection);
}

const struct http_server_connection_handler my_http_server_connection_handler = {
    .request = my_http_server_connection_request,
    .free = my_http_server_connection_free,
};
