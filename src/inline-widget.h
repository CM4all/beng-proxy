/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INLINE_WIDGET_H
#define BENG_PROXY_INLINE_WIDGET_H

#include "istream.h"

struct widget;
struct processor_env;

istream_t
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget);

#endif
