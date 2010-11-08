/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_GLUE_H
#define BENG_PROXY_WAS_GLUE_H

#include "istream.h"

#include <http/method.h>

struct was_stock;
struct hstock;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

/**
 * @param jail run the WAS application with JailCGI?
 */
void
was_request(pool_t pool, struct hstock *was_stock, bool jail,
            const char *action,
            const char *path,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            const char *document_root,
            struct strmap *headers, istream_t body,
            const char *const parameters[], unsigned num_parameters,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref);

#endif
