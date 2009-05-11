/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_GLUE_H
#define BENG_DELEGATE_GLUE_H

#include "delegate-client.h"

struct async_operation_ref;
struct hstock;

void
delegate_stock_open(struct hstock *stock, pool_t pool, const char *path,
                    delegate_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref);

#endif
