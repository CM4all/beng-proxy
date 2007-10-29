/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRANSLATE_H
#define __BENG_TRANSLATE_H

#include "pool.h"
#include "http.h"

struct config;

struct translate_request {
    const char *host;
    const char *uri;
    const char *session;
    const char *param;
};

struct translate_response {
    http_status_t status;
    const char *path;
    const char *content_type;
    const char *proxy;
    const char *redirect;
    const char *filter;
    int process;
    const char *session;
};

typedef void (*translate_callback_t)(const struct translate_response *response,
                                     void *ctx);

void
translate(pool_t pool,
          const struct config *config,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx);

#endif
