/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_GLUE_HXX
#define BENG_DELEGATE_GLUE_HXX

struct async_operation_ref;
struct ChildOptions;
struct StockMap;

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions *options,
                    const char *path,
                    const struct delegate_handler *handler, void *ctx,
                    struct async_operation_ref &async_ref);

#endif
