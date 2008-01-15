/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PARSER_H
#define __BENG_PARSER_H

#include "strref.h"
#include "compiler.h"

#include <sys/types.h>

struct pool;

enum parser_state {
    PARSER_NONE,

    /** parsing an element name */
    PARSER_ELEMENT_NAME,

    /** inside the element tag */
    PARSER_ELEMENT_TAG,

    /** parsing attribute name */
    PARSER_ATTR_NAME,

    /** after the attribute name, waiting for '=' */
    PARSER_AFTER_ATTR_NAME,

    /** after the '=', waiting for the attribute value */
    PARSER_BEFORE_ATTR_VALUE,

    /** parsing the quoted attribute value */
    PARSER_ATTR_VALUE,

    /** compatibility with older and broken HTML: attribute value
        without quotes */
    PARSER_ATTR_VALUE_COMPAT,

    /** found a slash, waiting for the '>' */
    PARSER_SHORT,

    /** inside the element, currently unused */
    PARSER_INSIDE,

    /** parsing a declaration name beginning with "<!" */
    PARSER_DECLARATION_NAME,

    /** within a CDATA section */
    PARSER_CDATA_SECTION,
};

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
    off_t value_start, value_end;
    struct strref name, value;
};

struct parser_handler {
    void (*tag_start)(const struct parser_tag *tag, void *ctx);
    void (*tag_finished)(const struct parser_tag *tag, void *ctx);
    void (*attr_finished)(const struct parser_attr *attr, void *ctx);
    void (*cdata)(const char *p, size_t length, int escaped, void *ctx);
};

struct parser {
    /* internal state */
    enum parser_state state;
    off_t position;

    /* element */
    struct parser_tag tag;
    char tag_name[64];
    size_t tag_name_length;

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    char attr_value[1024];
    size_t attr_value_length;
    struct parser_attr attr;

    /** in a CDATA section, how many characters have been matching
        CDEnd ("]]>")? */
    size_t cdend_match;

    const struct parser_handler *handler;
    void *handler_ctx;
};

struct parser * attr_malloc
parser_new(pool_t pool, const struct parser_handler *handler, void *handler_ctx);

void
parser_feed(struct parser *parser, off_t position, const char *start, size_t length);

#endif
