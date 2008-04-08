/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_HTTP_H
#define __BENG_WIDGET_HTTP_H

#include "pool.h"

struct widget;
struct processor_env;
struct http_response_handler;
struct async_operation_ref;

void
widget_http_request(pool_t pool, struct widget *widget,
                    struct processor_env *env,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
