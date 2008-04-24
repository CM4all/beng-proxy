/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PARSER_H
#define __BENG_PARSER_H

#include "strref.h"
#include "istream.h"

#include <inline/compiler.h>

#include <sys/types.h>

struct pool;

enum parser_tag_type {
    TAG_OPEN,
    TAG_CLOSE,
    TAG_SHORT,
};

struct parser_tag {
    off_t start, end;
    struct strref name;
    enum parser_tag_type type;
};

struct parser_attr {
    off_t name_start, value_start, value_end, end;
    struct strref name, value;
};

struct parser_handler {
    void (*tag_start)(const struct parser_tag *tag, void *ctx);
    void (*tag_finished)(const struct parser_tag *tag, void *ctx);
    void (*attr_finished)(const struct parser_attr *attr, void *ctx);
    size_t (*cdata)(const char *p, size_t length, bool escaped, void *ctx);
    void (*eof)(void *ctx, off_t length);
    void (*abort)(void *ctx);
};

struct parser;

struct parser * __attr_malloc
parser_new(pool_t pool, istream_t input,
           const struct parser_handler *handler, void *handler_ctx);

void
parser_close(struct parser *parser);

void
parser_read(struct parser *parser);

void
parser_script(struct parser *parser);

#endif
