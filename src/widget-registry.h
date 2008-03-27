/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_REGISTRY_H
#define __BENG_WIDGET_REGISTRY_H

#include "translate.h"

void
widget_registry_lookup(pool_t pool,
                       struct stock *translate_stock,
                       const char *widget_type,
                       translate_callback_t callback,
                       void *ctx,
                       struct async_operation_ref *async_ref);

struct processor_env;
struct widget;
struct http_response_handler;

void
widget_class_lookup(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
