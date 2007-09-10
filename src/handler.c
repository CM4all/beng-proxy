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

    translated = p_malloc(request->pool, sizeof(*translated));

    uri_parse(&translated->uri, request->uri);

    if (memcmp(request->uri, "/proxy/", 7) == 0) {
        /* XXX append query string */
        translated->path = p_strncat(request->pool,
                                     "http://dory.intern.cm-ag/~max/",
                                     sizeof("http://dory.intern.cm-ag/~max/") - 1,
                                     translated->uri.base + 7,
                                     translated->uri.base_length - 7,
                                     NULL);
    } else if (memcmp(request->uri, "/test/", 6) == 0) {
        /* XXX append query string */
        translated->path = p_strncat(request->pool,
                                     "http://cfatest01.intern.cm-ag/",
                                     sizeof("http://cfatest01.intern.cm-ag/") - 1,
                                     translated->uri.base + 6,
                                     translated->uri.base_length - 6,
                                     NULL);
    } else {
        /* XXX this is, of course, a huge security hole */
        translated->path = p_strncat(request->pool,
                                     "/var/www/",
                                     sizeof("/var/www/") - 1,
                                     translated->uri.base,
                                     translated->uri.base_length,
                                     NULL);
    }

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
