/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"
#include "config.h"
#include "translate.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
translate_callback(const struct translate_response *response,
                   void *ctx)
{
    struct http_server_request *request = ctx;
    struct translated *translated;
    int ret;

    if (response->status < 0 ||
        (response->path == NULL && response->proxy == NULL)) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    translated = p_malloc(request->pool, sizeof(*translated));

    ret = uri_parse(request->pool, &translated->uri, request->uri);
    if (ret < 0) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (response->path != NULL) {
        file_callback(request, response->path);
    } else if (response->proxy != NULL) {
        translated->path = response->proxy;
        proxy_callback(request, translated);
    } else {
        assert(0);
    }
}

static struct translated *
translate2(struct http_server_request *request,
           const struct config *config)
{
    struct translated *translated;
    int ret;

    if (config->translation_socket != NULL) {
        struct translate_request tr = {
            .host = strmap_get(request->headers, "host"),
            .uri = request->uri,
            /* XXX .session */
        };

        translate(request->pool, config, &tr, translate_callback, request);
        return NULL;
    }

    translated = p_malloc(request->pool, sizeof(*translated));

    ret = uri_parse(request->pool, &translated->uri, request->uri);
    if (ret < 0) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return NULL;
    }

    assert(translated->uri.base_length > 0);
    assert(translated->uri.base[0] == '/');

    translated->path = p_strncat(request->pool,
                                 config->document_root,
                                 strlen(config->document_root),
                                 translated->uri.base,
                                 translated->uri.base_length,
                                 NULL);

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

    translated = translate2(request, connection->config);
    if (translated == NULL)
        return;

    if (translated == NULL || translated->path == NULL) {
        http_server_send_message(request,
                                 HTTP_STATUS_NOT_FOUND,
                                 "The requested resource does not exist.");
        return;
    }

    file_callback(request, translated->path);
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
