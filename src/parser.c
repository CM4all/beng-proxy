/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "parser.h"
#include "pool.h"
#include "strutil.h"
#include "compiler.h"
#include "valgrind.h"

#include <assert.h>
#include <string.h>

struct parser * attr_malloc
parser_new(struct pool *pool, const struct parser_handler *handler, void *handler_ctx)
{
    struct parser *parser = p_malloc(pool, sizeof(*parser));

    assert(handler != NULL);
    assert(handler->tag_start != NULL);
    assert(handler->tag_finished != NULL);
    assert(handler->attr_finished != NULL);
    assert(handler->cdata != NULL);

    parser->state = PARSER_NONE;
    parser->handler = handler;
    parser->handler_ctx = handler_ctx;

    return parser;
}

static void
parser_invoke_attr_finished(struct parser *parser)
{
    strref_set(&parser->attr.name, parser->attr_name, parser->attr_name_length);
    strref_set(&parser->attr.value, parser->attr_value, parser->attr_value_length);

    parser->handler->attr_finished(&parser->attr, parser->handler_ctx);
    VALGRIND_MAKE_MEM_UNDEFINED(&parser->attr, sizeof(parser->attr));
}

void
parser_feed(struct parser *parser, off_t position, const char *start, size_t length)
{
    const char *buffer = start, *end = start + length, *p;

    assert(parser != NULL);
    assert(buffer != NULL);
    assert(length > 0);

    parser->position = position;

    while (buffer < end) {
        switch (parser->state) {
        case PARSER_NONE:
            /* find first character */
            p = memchr(buffer, '<', end - buffer);
            if (p == NULL) {
                parser->handler->cdata(buffer, end - buffer, 1,
                                       parser->handler_ctx);
                return;
            }

            if (p > buffer)
                parser->handler->cdata(buffer, p - buffer, 1,
                                       parser->handler_ctx);

            parser->tag.start = parser->position + (off_t)(p - start);
            parser->state = PARSER_ELEMENT_NAME;
            parser->tag_name_length = 0;
            parser->tag.type = TAG_OPEN;
            buffer = p + 1;
            break;

        case PARSER_ELEMENT_NAME:
            /* copy element name */
            while (buffer < end) {
                if (char_is_alphanumeric(*buffer) || *buffer == ':' || *buffer == '-' || *buffer == '_') {
                    if (parser->tag_name_length == sizeof(parser->tag_name)) {
                        /* name buffer overflowing */
                        parser->state = PARSER_NONE;
                        break;
                    }

                    parser->tag_name[parser->tag_name_length++] = char_to_lower(*buffer++);
                } else if (*buffer == '/' && parser->tag_name_length == 0) {
                    parser->tag.type = TAG_CLOSE;
                    ++buffer;
                } else if ((char_is_whitespace(*buffer) || *buffer == '/' || *buffer == '>') &&
                           parser->tag_name_length > 0) {
                    strref_set(&parser->tag.name, parser->tag_name, parser->tag_name_length);

                    parser->handler->tag_start(&parser->tag,
                                               parser->handler_ctx);
                    parser->state = PARSER_ELEMENT_TAG;
                    break;
                } else if (*buffer == '!' && parser->tag_name_length == 0) {
                    parser->state = PARSER_DECLARATION_NAME;
                    ++buffer;
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
                    parser->tag.type = TAG_SHORT;
                    parser->state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    parser->state = PARSER_INSIDE;
                    ++buffer;
                    parser->tag.end = parser->position + (off_t)(buffer - start);
                    parser->handler->tag_finished(&parser->tag,
                                                  parser->handler_ctx);
                    VALGRIND_MAKE_MEM_UNDEFINED(&parser->tag, sizeof(parser->tag));
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

                    parser->attr_name[parser->attr_name_length++] = char_to_lower(*buffer++);
                } else if (*buffer == '=' || char_is_whitespace(*buffer)) {
                    parser->state = PARSER_AFTER_ATTR_NAME;
                    break;
                } else {
                    parser_invoke_attr_finished(parser);
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
                    parser_invoke_attr_finished(parser);
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
                    parser->attr.value_start = parser->position + (off_t)(buffer - start);
                    break;
                } else if (char_is_whitespace(*buffer)) {
                    ++buffer;
                } else {
                    parser->state = PARSER_ATTR_VALUE_COMPAT;
                    parser->attr.value_start = parser->position + (off_t)(buffer - start);
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_ATTR_VALUE:
            /* wait till we find the delimiter */
            do {
                if (*buffer == parser->attr_value_delimiter) {
                    parser->attr.value_end = parser->position + (off_t)(buffer - start);
                    ++buffer;
                    parser_invoke_attr_finished(parser);
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
                if (!char_is_whitespace(*buffer) && *buffer != '>') {
                    if (parser->attr_value_length == sizeof(parser->attr_value)) {
                        /* XXX value buffer overflowing */
                        parser->state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    parser->attr_value[parser->attr_value_length++] = *buffer++;
                } else {
                    parser->attr.value_end = parser->position + (off_t)(buffer - start);
                    parser_invoke_attr_finished(parser);
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
                    parser->tag.end = parser->position + (off_t)(buffer - start);
                    parser->handler->tag_finished(&parser->tag,
                                                  parser->handler_ctx);
                    VALGRIND_MAKE_MEM_UNDEFINED(&parser->tag, sizeof(parser->tag));
                    break;
                } else {
                    /* ignore this syntax error and just close the
                       element tag */
                    parser->tag.end = parser->position + (off_t)(buffer - start);
                    parser->handler->tag_finished(&parser->tag,
                                                  parser->handler_ctx);
                    VALGRIND_MAKE_MEM_UNDEFINED(&parser->tag, sizeof(parser->tag));
                    parser->state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_INSIDE:
            /* XXX */
            parser->state = PARSER_NONE;
            break;

        case PARSER_DECLARATION_NAME:
            /* copy declaration element name */
            while (buffer < end) {
                if (char_is_alphanumeric(*buffer) || *buffer == ':' ||
                    *buffer == '-' || *buffer == '_' || *buffer == '[') {
                    if (parser->tag_name_length == sizeof(parser->tag_name)) {
                        /* name buffer overflowing */
                        parser->state = PARSER_NONE;
                        break;
                    }

                    parser->tag_name[parser->tag_name_length++] = char_to_lower(*buffer++);

                    if (parser->tag_name_length == 7 &&
                        memcmp(parser->tag_name, "[cdata[", 7) == 0) {
                        parser->state = PARSER_CDATA_SECTION;
                        parser->cdend_match = 0;
                        break;
                    }
                } else {
                    parser->state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_CDATA_SECTION:
            /* copy CDATA section contents */

            /* XXX this loop can be optimized with memchr() */
            p = buffer;
            while (buffer < end) {
                if (*buffer == ']' && parser->cdend_match < 2) {
                    if (buffer > p)
                        /* flush buffer */
                        parser->handler->cdata(p, buffer - p, 0,
                                               parser->handler_ctx);

                    p = ++buffer;
                    ++parser->cdend_match;
                } else if (*buffer == '>' && parser->cdend_match == 2) {
                    p = ++buffer;
                    parser->state = PARSER_NONE;
                    break;
                } else {
                    if (parser->cdend_match > 0) {
                        /* we had a partial match, and now we have to
                           restore the data we already skipped */
                        assert(parser->cdend_match < 3);

                        parser->handler->cdata("]]", parser->cdend_match, 0,
                                               parser->handler_ctx);
                        parser->cdend_match = 0;
                        p = buffer;
                    }

                    ++buffer;
                }
            }

            if (buffer > p)
                parser->handler->cdata(p, buffer - p, 0,
                                       parser->handler_ctx);

            break;
        }
    }
}
