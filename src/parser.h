/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PARSER_H
#define __BENG_PARSER_H

#include <sys/types.h>

#include "strref.h"

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

struct parser {
    /* internal state */
    enum parser_state state;
    off_t position;

    /* element */
    off_t tag_offset;
    char tag_name[64];
    size_t tag_name_length;
    enum parser_tag_type tag_type;

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    char attr_value[1024];
    size_t attr_value_length;
    off_t attr_value_start, attr_value_end;

    /** in a CDATA section, how many characters have been matching
        CDEnd ("]]>")? */
    size_t cdend_match;
};

static inline void
parser_init(struct parser *parser)
{
    parser->state = PARSER_NONE;
}

void
parser_element_start(struct parser *parser, off_t offset,
                     enum parser_tag_type type,
                     const struct strref *name);

void
parser_element_finished(struct parser *parser, off_t end);

void
parser_attr_finished(struct parser *parser,
                     const struct strref *name,
                     const struct strref *value);

void
parser_cdata(struct parser *parser, const char *p, size_t length, int escaped);

void
parser_feed(struct parser *parser, const char *start, size_t length);

#endif
