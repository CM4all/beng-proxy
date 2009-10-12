/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_REQUEST_H
#define BENG_PROXY_DELEGATE_REQUEST_H

#include "pool.h"

struct hstock;
struct http_response_handler;
struct async_operation_ref;

void
delegate_stock_request(struct hstock *stock, pool_t pool,
                       const char *helper, const char *document_root,
                       bool jail,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref *async_ref);

#endif
