/*
 * Simple parser for CSS (Cascading Style Sheets).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_PARSER_HXX
#define BENG_PROXY_CSS_PARSER_HXX

#include "util/StringView.hxx"

#include <exception>

#include <sys/types.h>

struct pool;
class Istream;
struct CssParser;

struct CssParserValue {
    off_t start, end;
    StringView value;
};

struct CssParserHandler {
    /**
     * A class name was found.
     */
    void (*class_name)(const CssParserValue *name, void *ctx);

    /**
     * A XML id was found.
     */
    void (*xml_id)(const CssParserValue *id, void *ctx);

    /**
     * A new block begins.  Optional method.
     */
    void (*block)(void *ctx);

    /**
     * A property value with a keyword value.  Optional method.
     */
    void (*property_keyword)(const char *name, StringView value,
                             off_t start, off_t end, void *ctx);

    /**
     * A property value with a URL was found.  Optional method.
     */
    void (*url)(const CssParserValue *url, void *ctx);

    /**
     * The command "@import" was found.  Optional method.
     */
    void (*import)(const CssParserValue *url, void *ctx);

    /**
     * The CSS end-of-file was reached.
     */
    void (*eof)(void *ctx, off_t length);

    /**
     * An I/O error has occurred.
     */
    void (*error)(std::exception_ptr ep, void *ctx);
};

/**
 * @param block true when the input consists of only one block
 */
CssParser *
css_parser_new(struct pool &pool, Istream &input, bool block,
               const CssParserHandler &handler, void *handler_ctx);

/**
 * Force-closen the CSS parser, don't invoke any handler methods.
 */
void
css_parser_close(CssParser *parser);

/**
 * Ask the CSS parser to read and parse more CSS source code.  Does
 * nothing if the istream blocks.
 */
void
css_parser_read(CssParser *parser);

#endif
