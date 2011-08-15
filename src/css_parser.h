/*
 * Simple parser for CSS (Cascading Style Sheets).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_PARSER_H
#define BENG_PROXY_CSS_PARSER_H

#include "strref.h"

#include <glib.h>

#include <sys/types.h>

struct pool;
struct istream;

struct css_parser_url {
    off_t start, end;
    struct strref value;
};

struct css_parser_handler {
    void (*url)(const struct css_parser_url *url, void *ctx);
    void (*eof)(void *ctx, off_t length);
    void (*error)(GError *error, void *ctx);
};

struct css_parser *
css_parser_new(struct pool *pool, struct istream *input,
               const struct css_parser_handler *handler, void *handler_ctx);

void
css_parser_close(struct css_parser *parser);

void
css_parser_read(struct css_parser *parser);

#endif
