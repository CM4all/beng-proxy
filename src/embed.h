/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_EMBED_H
#define __BENG_EMBED_H

#include "istream.h"
#include "http.h"

struct widget;
struct processor_env;

istream_t
embed_new(pool_t pool, struct widget *widget,
          struct processor_env *env,
          unsigned options);

istream_t
embed_iframe_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget);

istream_t
embed_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget);

#endif
