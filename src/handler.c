/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"

#include <stdio.h>

static struct translated *
translate(struct http_server_request *request)
{
    struct translated *translated;
    char path[1024];

    /* XXX this is, of course, a huge security hole */
    snprintf(path, sizeof(path), "/var/www/%s", request->uri);

    translated = p_malloc(request->pool, sizeof(*translated));
    translated->path = p_strdup(request->pool, path);
    translated->path_info = NULL;
    return translated;
}

void
my_http_server_callback(struct http_server_request *request,
                        void *ctx)
{
    struct client_connection *connection = ctx;
    struct translated *translated;

    if (request == NULL) {
        /* since remove_connection() might recurse here, we check if
           the connection has already been removed from the linked
           list */
        if (connection->http != NULL)
            remove_connection(connection);
        return;
    }

    (void)request;
    (void)connection;

    translated = translate(request);
    if (translated == NULL) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        http_server_response_finish(request->connection);
        return;
    }

    if (translated == NULL || translated->path == NULL) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_NOT_FOUND,
                                 "The requested resource does not exist.");
        http_server_response_finish(request->connection);
        return;
    }

    file_callback(connection, request, translated);
}
