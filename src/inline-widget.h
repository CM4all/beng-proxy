/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INLINE_WIDGET_H
#define BENG_PROXY_INLINE_WIDGET_H

struct pool;
struct widget;
struct processor_env;

struct istream *
embed_inline_widget(struct pool *pool, struct processor_env *env,
                    struct widget *widget);

#endif
