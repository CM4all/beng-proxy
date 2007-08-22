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
    PARSER_START,
    PARSER_NAME,
    PARSER_ELEMENT,
    PARSER_SHORT,
    PARSER_INSIDE,
};

struct parser {
    enum parser_state parser_state;
    off_t source_length;
    off_t element_offset;
    size_t match_length;
    char element_name[64];
    size_t element_name_length;
};

void
parser_element_finished(struct parser *parser, off_t end);

void
parser_feed(struct parser *parser, const char *start, size_t length);

#endif
