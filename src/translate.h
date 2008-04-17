/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRANSLATE_H
#define __BENG_TRANSLATE_H

#include "pool.h"
#include "http.h"

struct uri_with_address;
struct stock;
struct async_operation_ref;

struct translate_request {
    const char *remote_host;
    const char *host;
    const char *uri;
    const char *widget_type;
    const char *session;
    const char *param;
};

struct translate_transformation {
    struct translate_transformation *next;

    enum {
        TRANSFORMATION_PROCESS,
        TRANSFORMATION_FILTER,
    } type;

    union {
        unsigned processor_options;

        const char *filter;
    } u;
};

struct translate_response {
    http_status_t status;
    const char *path;
    const char *path_info;
    const char *site;
    const char *document_root;
    const char *content_type;
    struct uri_with_address *proxy;
    const char *redirect;
    bool cgi;
    bool google_gadget;
    const char *session;
    const char *user;
    const char *language;

    struct translate_transformation *transformation;
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
          void *ctx,
          struct async_operation_ref *async_ref);

#endif
