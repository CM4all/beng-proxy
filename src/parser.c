/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "parser.h"
#include "strutil.h"
#include "compiler.h"

#include <assert.h>
#include <string.h>

void
parser_feed(struct parser *parser, const char *start, size_t length)
{
    const char *buffer = start, *end = start + length, *p;

    assert(parser != NULL);
    assert(buffer != NULL);
    assert(length > 0);

    while (buffer < end) {
        switch (parser->state) {
        case PARSER_NONE:
            /* find first character */
            p = memchr(buffer, '<', end - buffer);
            if (p == NULL)
                return;

            parser->element_offset = parser->position + (off_t)(p - start);
            parser->state = PARSER_ELEMENT_NAME;
            parser->element_name_length = 0;
            buffer = p + 1;
            break;

        case PARSER_ELEMENT_NAME:
            /* copy element name */
            while (buffer < end) {
                if (char_is_alphanumeric(*buffer) || *buffer == ':' || *buffer == '-' || *buffer == '_') {
                    if (parser->element_name_length == sizeof(parser->element_name)) {
                        /* name buffer overflowing */
                        parser->state = PARSER_NONE;
                        break;
                    }

                    parser->element_name[parser->element_name_length++] = *buffer++;
                } else if ((char_is_whitespace(*buffer) || *buffer == '/' || *buffer == '>') &&
                           parser->element_name_length > 0) {
                    parser_element_start(parser);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                } else {
                    parser->state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_ELEMENT_TAG:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '/') {
                    parser->state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    parser->state = PARSER_INSIDE;
                    ++buffer;
                    parser_element_finished(parser, parser->position + (off_t)(buffer - start));
                    break;
                } else if (char_is_letter(*buffer)) {
                    parser->state = PARSER_ATTR_NAME;
                    parser->attr_name_length = 0;
                    parser->attr_value_length = 0;
                    break;
                } else {
                    parser->state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_ATTR_NAME:
            /* copy attribute name */
            do {
                if (char_is_alphanumeric(*buffer) || *buffer == ':') {
                    if (parser->attr_name_length == sizeof(parser->attr_name)) {
                        /* name buffer overflowing */
                        parser->state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    parser->attr_name[parser->attr_name_length++] = *buffer++;
                } else if (*buffer == '=' || char_is_whitespace(*buffer)) {
                    parser->state = PARSER_AFTER_ATTR_NAME;
                    break;
                } else {
                    parser_attr_finished(parser);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_AFTER_ATTR_NAME:
            /* wait till we find '=' */
            do {
                if (*buffer == '=') {
                    parser->state = PARSER_BEFORE_ATTR_VALUE;
                    ++buffer;
                    break;
                } else if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else {
                    parser_attr_finished(parser);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_BEFORE_ATTR_VALUE:
            do {
                if (*buffer == '"' || *buffer == '\'') {
                    parser->state = PARSER_ATTR_VALUE;
                    parser->attr_value_delimiter = *buffer;
                    ++buffer;
                    parser->attr_value_start = parser->position + (off_t)(buffer - start);
                    break;
                } else if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else {
                    parser->state = PARSER_ATTR_VALUE_COMPAT;
                    parser->attr_value_start = parser->position + (off_t)(buffer - start);
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_ATTR_VALUE:
            /* wait till we find the delimiter */
            do {
                if (*buffer == parser->attr_value_delimiter) {
                    parser->attr_value_end = parser->position + (off_t)(buffer - start);
                    ++buffer;
                    parser_attr_finished(parser);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                } else {
                    if (parser->attr_value_length == sizeof(parser->attr_value)) {
                        /* XXX value buffer overflowing */
                        parser->state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    parser->attr_value[parser->attr_value_length++] = *buffer++;
                }
            } while (buffer < end);

            break;

        case PARSER_ATTR_VALUE_COMPAT:
            /* wait till the value is finished */
            do {
                if (char_is_alphanumeric(*buffer) || *buffer == '_' || *buffer == '-') {
                    if (parser->attr_value_length == sizeof(parser->attr_value)) {
                        /* XXX value buffer overflowing */
                        parser->state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    parser->attr_value[parser->attr_value_length++] = *buffer++;
                } else {
                    parser->attr_value_end = parser->position + (off_t)(buffer - start);
                    parser_attr_finished(parser);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_SHORT:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '>') {
                    parser->state = PARSER_NONE;
                    ++buffer;
                    parser_element_finished(parser, parser->position + (off_t)(buffer - start));
                    break;
                } else {
                    parser->state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_INSIDE:
            /* XXX */
            parser->state = PARSER_NONE;
            break;
        }
    }
}
