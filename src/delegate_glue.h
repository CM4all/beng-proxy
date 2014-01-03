/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_GLUE_H
#define BENG_DELEGATE_GLUE_H

#include "delegate_client.h"

struct async_operation_ref;
struct hstock;
struct child_options;

void
delegate_stock_open(struct hstock *stock, struct pool *pool,
                    const char *helper,
                    const struct child_options *options,
                    const char *path,
                    const struct delegate_handler *handler, void *ctx,
                    struct async_operation_ref *async_ref);

#endif
