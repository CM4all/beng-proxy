/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "connection.h"
#include "config.h"
#include "translate.h"
#include "uri.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct translate_ctx {
    struct http_server_request *request;
    struct parsed_uri uri;
    struct translate_request tr;
};

static void
translate_callback(const struct translate_response *response,
                   void *_ctx)
{
    struct translate_ctx *ctx = _ctx;
    struct http_server_request *request = ctx->request;

    if (response->status < 0 ||
        (response->path == NULL && response->proxy == NULL)) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (response->path != NULL) {
        file_callback(request, &ctx->uri, response->path);
    } else if (response->proxy != NULL) {
        proxy_callback(request, &ctx->uri, response->proxy);
    } else {
        assert(0);
    }
}

static void
ask_translation_server(struct http_server_request *request,
                       const struct config *config)
{
    struct translate_ctx *ctx;
    int ret;

    ctx = p_malloc(request->pool, sizeof(*ctx));

    ret = uri_parse(request->pool, &ctx->uri, request->uri);
    if (ret < 0) {
        http_server_send_message(request,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Malformed URI");
        return;
    }

    ctx->request = request;
    ctx->tr.host = strmap_get(request->headers, "host");
    ctx->tr.uri = p_strndup(request->pool, ctx->uri.base, ctx->uri.base_length);
    ctx->tr.session = NULL; /* XXX */

    translate(request->pool, config, &ctx->tr, translate_callback, ctx);
}

static void
serve_document_root_file(struct http_server_request *request,
                         const struct config *config)
{
    int ret;
    struct parsed_uri *uri;
    const char *path, *index_file = NULL;

    uri = p_malloc(request->pool, sizeof(*uri));
    ret = uri_parse(request->pool, uri, request->uri);
    if (ret < 0) {
        http_server_send_message(request,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Malformed URI");
        return;
    }

    assert(uri->base_length > 0);
    assert(uri->base[0] == '/');

    if (uri->base[uri->base_length - 1] == '/')
        index_file = "index.html";

    path = p_strncat(request->pool,
                     config->document_root,
                     strlen(config->document_root),
                     uri->base,
                     uri->base_length,
                     index_file, 10,
                     NULL);

    file_callback(request, uri, path);
}

static void
my_http_server_connection_request(struct http_server_request *request,
                                  void *ctx)
{
    struct client_connection *connection = ctx;

    assert(request != NULL);

    if (connection->config->translation_socket == NULL) {
        serve_document_root_file(request, connection->config);
    } else {
        ask_translation_server(request, connection->config);
    }
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
