/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRANSLATE_H
#define __BENG_TRANSLATE_H

#include "pool.h"
#include "http.h"

struct stock;

struct translate_request {
    const char *remote_host;
    const char *host;
    const char *uri;
    const char *session;
    const char *param;
};

struct translate_response {
    http_status_t status;
    const char *path;
    const char *path_info;
    const char *site;
    const char *content_type;
    const char *proxy;
    const char *redirect;
    const char *filter;
    int process, cgi;
    const char *session;
    const char *user;
    const char *language;
};

typedef void (*translate_callback_t)(const struct translate_response *response,
                                     void *ctx);

struct stock *
translate_stock_new(pool_t pool, const char *translation_socket);

void
translate(pool_t pool,
          struct stock *stock,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx);

#endif
