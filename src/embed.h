/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_EMBED_H
#define __BENG_EMBED_H

#include "istream.h"

struct widget;
struct processor_env;
struct http_response_handler;
struct async_operation_ref;

istream_t
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget);

void
embed_frame_widget(pool_t pool, struct processor_env *env,
                   struct widget *widget,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

#endif
