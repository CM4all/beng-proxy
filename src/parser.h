/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PARSER_H
#define __BENG_PARSER_H

#include <sys/types.h>

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
};

struct parser {
    /* internal state */
    enum parser_state state;
    off_t position;

    /* element */
    off_t element_offset;
    char element_name[64];
    size_t element_name_length;
    enum {
        TAG_OPEN,
        TAG_CLOSE,
        TAG_SHORT,
    } tag_type;

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    char attr_value[1024];
    size_t attr_value_length;
    off_t attr_value_start, attr_value_end;
};

static inline void
parser_init(struct parser *parser)
{
    parser->state = PARSER_NONE;
}

void
parser_element_start(struct parser *parser);

void
parser_element_finished(struct parser *parser, off_t end);

void
parser_attr_finished(struct parser *parser);

void
parser_cdata(struct parser *parser, const char *p, size_t length);

void
parser_feed(struct parser *parser, const char *start, size_t length);

#endif
