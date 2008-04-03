/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FRAME_H
#define __BENG_FRAME_H

#include "istream.h"

struct widget;
struct processor_env;
struct http_response_handler;
struct async_operation_ref;

void
embed_frame_widget(pool_t pool, struct processor_env *env,
                   struct widget *widget,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

istream_t
frame_widget_html_iframe(pool_t pool, const struct processor_env *env,
                         struct widget *widget);

#endif
