/*
 * Emulation layer for Google gadgets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GOOGLE_GADGET_H
#define __BENG_GOOGLE_GADGET_H

#include "pool.h"

struct processor_env;
struct widget;
struct http_response_handler;
struct async_operation_ref;

void
embed_google_gadget(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
