/*
 * Generate JavaScript snippets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_JS_GENERATOR_H
#define __BENG_JS_GENERATOR_H

#include "pool.h"
#include "istream.h"

struct growing_buffer;
struct widget;

void
js_generate_widget(struct growing_buffer *gb, const struct widget *widget,
                   pool_t pool);

void
js_generate_includes(struct growing_buffer *gb);

void
js_generate_root_widget(struct growing_buffer *gb, const char *session_id);

void
js_generate_preferences(struct growing_buffer *gb, const struct widget *widget);

istream_t
js_generate_tail(pool_t pool);

#endif
