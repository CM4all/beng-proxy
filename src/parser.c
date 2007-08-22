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

static const char element_start[] = "<c:";
static const char element_end[] = "</c:";

void
parser_feed(struct parser *parser, const char *start, size_t length)
{
    const char *buffer = start, *end = start + length, *p;

    assert(parser != NULL);
    assert(buffer != NULL);
    assert(length > 0);

    while (buffer < end) {
        switch (parser->parser_state) {
        case PARSER_NONE:
            /* find first character */
            p = memchr(buffer, element_start[0], end - buffer);
            if (p == NULL)
                return;

            parser->parser_state = PARSER_START;
            parser->element_offset = parser->source_length + (off_t)(p - start);
            parser->match_length = 1;
            buffer = p + 1;
            break;

        case PARSER_START:
            /* compare more characters */
            assert(parser->match_length > 0);
            assert(parser->match_length < sizeof(element_start) - 1);

            do {
                if (*buffer != element_start[parser->match_length]) {
                    parser->parser_state = PARSER_NONE;
                    break;
                }

                ++parser->match_length;
                ++buffer;

                if (parser->match_length == sizeof(element_start) - 1) {
                    parser->parser_state = PARSER_NAME;
                    parser->element_name_length = 0;
                    break;
                }
            } while (unlikely(buffer < end));

            break;

        case PARSER_NAME:
            /* copy element name */
            while (buffer < end) {
                if (char_is_alphanumeric(*buffer)) {
                    if (parser->element_name_length == sizeof(parser->element_name)) {
                        /* name buffer overflowing */
                        parser->parser_state = PARSER_NONE;
                        break;
                    }

                    parser->element_name[parser->element_name_length++] = *buffer++;
                } else if ((char_is_whitespace(*buffer) || *buffer == '/' || *buffer == '>') &&
                           parser->element_name_length > 0) {
                    parser->parser_state = PARSER_ELEMENT;
                    break;
                } else {
                    parser->parser_state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_ELEMENT:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '/') {
                    parser->parser_state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    parser->parser_state = PARSER_INSIDE;
                    ++buffer;
                    parser_element_finished(parser, parser->source_length + (off_t)(buffer - start));
                    break;
                } else {
                    parser->parser_state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_SHORT:
            do {
                if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else if (*buffer == '>') {
                    parser->parser_state = PARSER_NONE;
                    ++buffer;
                    parser_element_finished(parser, parser->source_length + (off_t)(buffer - start));
                    break;
                } else {
                    parser->parser_state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_INSIDE:
            /* XXX */
            parser->parser_state = PARSER_NONE;
            break;
        }
    }
}
