/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_REQUEST_HXX
#define BENG_PROXY_DELEGATE_REQUEST_HXX

struct pool;
struct hstock;
struct http_response_handler;
struct async_operation_ref;
struct child_options;

void
delegate_stock_request(struct hstock *stock, struct pool *pool,
                       const char *helper,
                       const struct child_options *options,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref &async_ref);

#endif
