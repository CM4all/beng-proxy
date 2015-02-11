/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_GLUE_HXX
#define BENG_PROXY_WAS_GLUE_HXX

#include <http/method.h>

struct pool;
struct istream;
struct was_stock;
struct StockMap;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct ChildOptions;
template<typename T> struct ConstBuffer;

/**
 * @param jail run the WAS application with JailCGI?
 * @param args command-line arguments
 */
void
was_request(struct pool *pool, StockMap *was_stock,
            const ChildOptions *options,
            const char *action,
            const char *path,
            ConstBuffer<const char *> args,
            ConstBuffer<const char *> env,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            struct strmap *headers, struct istream *body,
            ConstBuffer<const char *> params,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref);

#endif
