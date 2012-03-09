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

struct css_parser_value {
    off_t start, end;
    struct strref value;
};

struct css_parser_handler {
    /**
     * A class name was found.
     */
    void (*class_name)(const struct css_parser_value *name, void *ctx);

    /**
     * A XML id was found.
     */
    void (*xml_id)(const struct css_parser_value *id, void *ctx);

    /**
     * A new block begins.  Optional method.
     */
    void (*block)(void *ctx);

    /**
     * A property value with a keyword value.  Optional method.
     */
    void (*property_keyword)(const char *name, const char *value,
                             off_t start, off_t end, void *ctx);

    /**
     * A property value with a URL was found.  Optional method.
     */
    void (*url)(const struct css_parser_value *url, void *ctx);

    /**
     * The command "@import" was found.  Optional method.
     */
    void (*import)(const struct css_parser_value *url, void *ctx);

    /**
     * The CSS end-of-file was reached.
     */
    void (*eof)(void *ctx, off_t length);

    /**
     * An I/O error has occurred.
     */
    void (*error)(GError *error, void *ctx);
};

struct css_parser *
css_parser_new(struct pool *pool, struct istream *input,
               const struct css_parser_handler *handler, void *handler_ctx);

/**
 * Force-closen the CSS parser, don't invoke any handler methods.
 */
void
css_parser_close(struct css_parser *parser);

/**
 * Ask the CSS parser to read and parse more CSS source code.  Does
 * nothing if the istream blocks.
 */
void
css_parser_read(struct css_parser *parser);

#endif
