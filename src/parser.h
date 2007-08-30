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
    PARSER_NAME,
    PARSER_ELEMENT,
    PARSER_ATTR_NAME,
    PARSER_AFTER_ATTR_NAME,
    PARSER_BEFORE_ATTR_VALUE,
    PARSER_ATTR_VALUE,
    PARSER_ATTR_VALUE_COMPAT,
    PARSER_SHORT,
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

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    char attr_value[1024];
    size_t attr_value_length;
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
parser_attr_finished(struct parser *parser, off_t end);

void
parser_feed(struct parser *parser, const char *start, size_t length);

#endif
