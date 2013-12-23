/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_REQUEST_H
#define __BENG_FCGI_REQUEST_H

#include <http/method.h>

struct pool;
struct istream;
struct fcgi_stock;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct jail_params;

/**
 * @param jail run the FastCGI application with JailCGI?
 * @param args command-line arguments
 */
void
fcgi_request(struct pool *pool, struct fcgi_stock *fcgi_stock,
             const struct jail_params *jail,
             bool user_namespace, bool network_namespace,
             const char *action,
             const char *path,
             const char *const*args, unsigned n_args,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             struct strmap *headers, struct istream *body,
             const char *const env[], unsigned n_env,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif
