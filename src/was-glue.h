/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_GLUE_H
#define BENG_PROXY_WAS_GLUE_H

#include <http/method.h>

struct pool;
struct istream;
struct was_stock;
struct hstock;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct jail_params;

/**
 * @param jail run the WAS application with JailCGI?
 * @param args command-line arguments
 */
void
was_request(struct pool *pool, struct hstock *was_stock,
            const struct jail_params *jail,
            const char *action,
            const char *path,
            const char *const*args, unsigned n_args,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            struct strmap *headers, struct istream *body,
            const char *const parameters[], unsigned num_parameters,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref);

#endif
