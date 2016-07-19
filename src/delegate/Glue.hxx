/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_GLUE_HXX
#define BENG_DELEGATE_GLUE_HXX

struct ChildOptions;
class StockMap;
class DelegateHandler;
class CancellablePointer;

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    DelegateHandler &handler,
                    CancellablePointer &cancel_ptr);

#endif
