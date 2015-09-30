/*
 * Parse CM4all commands in HTML documents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "xml_parser.hxx"
#include "pool.hxx"
#include "html_chars.hxx"
#include "expansible_buffer.hxx"
#include "istream/istream.hxx"
#include "util/CharUtil.hxx"

#include <inline/poison.h>

#include <assert.h>
#include <string.h>

enum parser_state {
    PARSER_NONE,

    /** within a SCRIPT element; only accept "</" to break out */
    PARSER_SCRIPT,

    /** found '<' within a SCRIPT element */
    PARSER_SCRIPT_ELEMENT_NAME,

    /** parsing an element name */
    PARSER_ELEMENT_NAME,

    /** inside the element tag */
    PARSER_ELEMENT_TAG,

    /** inside the element tag, but ignore attributes */
    PARSER_ELEMENT_BORING,

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

    /** within a comment */
    PARSER_COMMENT,
};

class XmlParser {
public:
    struct pool *pool;

    struct istream *input;
    off_t position;

    /* internal state */
    enum parser_state state;

    /* element */
    XmlParserTag tag;
    char tag_name[64];
    size_t tag_name_length;

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    struct expansible_buffer *attr_value;
    XmlParserAttribute attr;

    /** in a CDATA section, how many characters have been matching
        CDEnd ("]]>")? */
    size_t cdend_match;

    /** in a comment, how many consecutive minus are there? */
    unsigned minus_count;

    const XmlParserHandler *handler;
    void *handler_ctx;

    void InvokeAttributeFinished() {
        strref_set(&attr.name, attr_name, attr_name_length);
        expansible_buffer_read_strref(attr_value, &attr.value);

        handler->attr_finished(&attr, handler_ctx);
        poison_undefined(&attr, sizeof(attr));
    }

    size_t Feed(const char *start, size_t length);
};

inline size_t
XmlParser::Feed(const char *start, size_t length)
{
    const char *buffer = start, *end = start + length, *p;
    size_t nbytes;

    assert(input != nullptr);
    assert(buffer != nullptr);
    assert(length > 0);

    while (buffer < end) {
        switch (state) {
        case PARSER_NONE:
        case PARSER_SCRIPT:
            /* find first character */
            p = (const char *)memchr(buffer, '<', end - buffer);
            if (p == nullptr) {
                nbytes = handler->cdata(buffer, end - buffer, 1,
                                        position + buffer - start,
                                        handler_ctx);
                assert(nbytes <= (size_t)(end - buffer));

                if (input == nullptr)
                    return 0;

                nbytes += buffer - start;
                position += (off_t)nbytes;
                return nbytes;
            }

            if (p > buffer) {
                nbytes = handler->cdata(buffer, p - buffer, 1,
                                        position + buffer - start,
                                        handler_ctx);
                assert(nbytes <= (size_t)(p - buffer));

                if (input == nullptr)
                    return 0;

                if (nbytes < (size_t)(p - buffer)) {
                    nbytes += buffer - start;
                    position += (off_t)nbytes;
                    return nbytes;
                }
            }

            tag.start = position + (off_t)(p - start);
            state = state == PARSER_NONE
                ? PARSER_ELEMENT_NAME
                : PARSER_SCRIPT_ELEMENT_NAME;
            tag_name_length = 0;
            tag.type = TAG_OPEN;
            buffer = p + 1;
            break;

        case PARSER_SCRIPT_ELEMENT_NAME:
            if (*buffer == '/') {
                state = PARSER_ELEMENT_NAME;
                tag.type = TAG_CLOSE;
                ++buffer;
            } else {
                nbytes = handler->cdata("<", 1, 1,
                                        position + buffer - start,
                                        handler_ctx);
                assert(nbytes <= (size_t)(end - buffer));

                if (input == nullptr)
                    return 0;

                if (nbytes == 0) {
                    nbytes = buffer - start;
                    position += nbytes;
                    return nbytes;
                }

                state = PARSER_SCRIPT;
            }

            break;

        case PARSER_ELEMENT_NAME:
            /* copy element name */
            while (buffer < end) {
                if (is_html_name_char(*buffer)) {
                    if (tag_name_length == sizeof(tag_name)) {
                        /* name buffer overflowing */
                        state = PARSER_NONE;
                        break;
                    }

                    tag_name[tag_name_length++] = ToLowerASCII(*buffer++);
                } else if (*buffer == '/' && tag_name_length == 0) {
                    tag.type = TAG_CLOSE;
                    ++buffer;
                } else if (*buffer == '?' && tag_name_length == 0) {
                    /* start of processing instruction */
                    tag.type = TAG_PI;
                    ++buffer;
                } else if ((IsWhitespaceOrNull(*buffer) || *buffer == '/' ||
                            *buffer == '?' || *buffer == '>') &&
                           tag_name_length > 0) {
                    bool interesting;

                    strref_set(&tag.name, tag_name, tag_name_length);

                    interesting = handler->tag_start(&tag, handler_ctx);

                    if (input == nullptr)
                        return 0;

                    state = interesting ? PARSER_ELEMENT_TAG : PARSER_ELEMENT_BORING;
                    break;
                } else if (*buffer == '!' && tag_name_length == 0) {
                    state = PARSER_DECLARATION_NAME;
                    ++buffer;
                    break;
                } else {
                    state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_ELEMENT_TAG:
            do {
                if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else if (*buffer == '/' && tag.type == TAG_OPEN) {
                    tag.type = TAG_SHORT;
                    state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '?' && tag.type == TAG_PI) {
                    state = PARSER_SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    state = PARSER_INSIDE;
                    ++buffer;
                    tag.end = position + (off_t)(buffer - start);
                    handler->tag_finished(&tag, handler_ctx);
                    poison_undefined(&tag, sizeof(tag));

                    if (input == nullptr)
                        return 0;

                    break;
                } else if (is_html_name_start_char(*buffer)) {
                    state = PARSER_ATTR_NAME;
                    attr.name_start = position + (off_t)(buffer - start);
                    attr_name_length = 0;
                    expansible_buffer_reset(attr_value);
                    break;
                } else {
                    /* ignore this syntax error and just close the
                       element tag */

                    tag.end = position + (off_t)(buffer - start);
                    state = PARSER_INSIDE;
                    handler->tag_finished(&tag, handler_ctx);

                    state = PARSER_NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_ELEMENT_BORING:
            /* ignore this tag */

            p = (const char *)memchr(buffer, '>', end - buffer);
            if (p != nullptr) {
                /* the "boring" tag has been closed */
                buffer = p + 1;
                state = PARSER_NONE;
            } else
                buffer = end;
            break;

        case PARSER_ATTR_NAME:
            /* copy attribute name */
            do {
                if (is_html_name_char(*buffer)) {
                    if (attr_name_length == sizeof(attr_name)) {
                        /* name buffer overflowing */
                        state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    attr_name[attr_name_length++] = ToLowerASCII(*buffer++);
                } else if (*buffer == '=' || IsWhitespaceOrNull(*buffer)) {
                    state = PARSER_AFTER_ATTR_NAME;
                    break;
                } else {
                    InvokeAttributeFinished();
                    state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_AFTER_ATTR_NAME:
            /* wait till we find '=' */
            do {
                if (*buffer == '=') {
                    state = PARSER_BEFORE_ATTR_VALUE;
                    ++buffer;
                    break;
                } else if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else {
                    InvokeAttributeFinished();
                    state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_BEFORE_ATTR_VALUE:
            do {
                if (*buffer == '"' || *buffer == '\'') {
                    state = PARSER_ATTR_VALUE;
                    attr_value_delimiter = *buffer;
                    ++buffer;
                    attr.value_start = position + (off_t)(buffer - start);
                    break;
                } else if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else {
                    state = PARSER_ATTR_VALUE_COMPAT;
                    attr.value_start = position + (off_t)(buffer - start);
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_ATTR_VALUE:
            /* wait till we find the delimiter */
            p = (const char *)memchr(buffer, attr_value_delimiter,
                                     end - buffer);
            if (p == nullptr) {
                if (!expansible_buffer_write_buffer(attr_value,
                                                    buffer, end - buffer)) {
                    state = PARSER_ELEMENT_TAG;
                    break;
                }

                buffer = end;
            } else {
                if (!expansible_buffer_write_buffer(attr_value,
                                                    buffer, p - buffer)) {
                    state = PARSER_ELEMENT_TAG;
                    break;
                }

                buffer = p + 1;
                attr.end = position + (off_t)(buffer - start);
                attr.value_end = attr.end - 1;
                InvokeAttributeFinished();
                state = PARSER_ELEMENT_TAG;
            }

            break;

        case PARSER_ATTR_VALUE_COMPAT:
            /* wait till the value is finished */
            do {
                if (!IsWhitespaceOrNull(*buffer) && *buffer != '>') {
                    if (!expansible_buffer_write_buffer(attr_value,
                                                        buffer, 1)) {
                        state = PARSER_ELEMENT_TAG;
                        break;
                    }

                    ++buffer;
                } else {
                    attr.value_end = attr.end =
                        position + (off_t)(buffer - start);
                    InvokeAttributeFinished();
                    state = PARSER_ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_SHORT:
            do {
                if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else if (*buffer == '>') {
                    state = PARSER_NONE;
                    ++buffer;
                    tag.end = position + (off_t)(buffer - start);
                    handler->tag_finished(&tag, handler_ctx);
                    poison_undefined(&tag, sizeof(tag));

                    if (input == nullptr)
                        return 0;

                    break;
                } else {
                    /* ignore this syntax error and just close the
                       element tag */

                    tag.end = position + (off_t)(buffer - start);
                    state = PARSER_INSIDE;
                    handler->tag_finished(&tag, handler_ctx);
                    poison_undefined(&tag, sizeof(tag));
                    state = PARSER_NONE;

                    if (input == nullptr)
                        return 0;

                    break;
                }
            } while (buffer < end);

            break;

        case PARSER_INSIDE:
            /* XXX */
            state = PARSER_NONE;
            break;

        case PARSER_DECLARATION_NAME:
            /* copy declaration element name */
            while (buffer < end) {
                if (IsAlphaNumericASCII(*buffer) || *buffer == ':' ||
                    *buffer == '-' || *buffer == '_' || *buffer == '[') {
                    if (tag_name_length == sizeof(tag_name)) {
                        /* name buffer overflowing */
                        state = PARSER_NONE;
                        break;
                    }

                    tag_name[tag_name_length++] = ToLowerASCII(*buffer++);

                    if (tag_name_length == 7 &&
                        memcmp(tag_name, "[cdata[", 7) == 0) {
                        state = PARSER_CDATA_SECTION;
                        cdend_match = 0;
                        break;
                    }

                    if (tag_name_length == 2 &&
                        memcmp(tag_name, "--", 2) == 0) {
                        state = PARSER_COMMENT;
                        minus_count = 0;
                        break;
                    }
                } else {
                    state = PARSER_NONE;
                    break;
                }
            }

            break;

        case PARSER_CDATA_SECTION:
            /* copy CDATA section contents */

            /* XXX this loop can be optimized with memchr() */
            p = buffer;
            while (buffer < end) {
                if (*buffer == ']' && cdend_match < 2) {
                    if (buffer > p) {
                        /* flush buffer */

                        size_t cdata_length = buffer - p;
                        off_t cdata_end = position + buffer - start;
                        off_t cdata_start = cdata_end - cdata_length;

                        nbytes = handler->cdata(p, cdata_length, false,
                                                cdata_start,
                                                handler_ctx);
                        assert(nbytes <= (size_t)(buffer - p));

                        if (input == nullptr)
                            return 0;

                        if (nbytes < (size_t)(buffer - p)) {
                            nbytes += p - start;
                            position += (off_t)nbytes;
                            return nbytes;
                        }
                    }

                    p = ++buffer;
                    ++cdend_match;
                } else if (*buffer == '>' && cdend_match == 2) {
                    p = ++buffer;
                    state = PARSER_NONE;
                    break;
                } else {
                    if (cdend_match > 0) {
                        /* we had a partial match, and now we have to
                           restore the data we already skipped */
                        assert(cdend_match < 3);

                        nbytes = handler->cdata("]]", cdend_match, 0,
                                                position + buffer - start,
                                                handler_ctx);
                        assert(nbytes <= cdend_match);

                        if (input == nullptr)
                            return 0;

                        cdend_match -= nbytes;

                        if (cdend_match > 0) {
                            nbytes = buffer - start;
                            position += (off_t)nbytes;
                            return nbytes;
                        }

                        p = buffer;
                    }

                    ++buffer;
                }
            }

            if (buffer > p) {
                size_t cdata_length = buffer - p;
                off_t cdata_end = position + buffer - start;
                off_t cdata_start = cdata_end - cdata_length;

                nbytes = handler->cdata(p, cdata_length, false,
                                        cdata_start,
                                        handler_ctx);
                assert(nbytes <= (size_t)(buffer - p));

                if (input == nullptr)
                    return 0;

                if (nbytes < (size_t)(buffer - p)) {
                    nbytes += p - start;
                    position += (off_t)nbytes;
                    return nbytes;
                }
            }

            break;

        case PARSER_COMMENT:
            switch (minus_count) {
            case 0:
                /* find a minus which introduces the "-->" sequence */
                p = (const char *)memchr(buffer, '-', end - buffer);
                if (p != nullptr) {
                    /* found one - minus_count=1 and go to char after
                       minus */
                    buffer = p + 1;
                    minus_count = 1;
                } else
                    /* none found - skip this chunk */
                    buffer = end;

                break;

            case 1:
                if (*buffer == '-')
                    /* second minus found */
                    minus_count = 2;
                else
                    minus_count = 0;
                ++buffer;

                break;

            case 2:
                if (*buffer == '>')
                    /* end of comment */
                    state = PARSER_NONE;
                else if (*buffer == '-')
                    /* another minus... keep minus_count at 2 and go
                       to next character */
                    ++buffer;
                else
                    minus_count = 0;

                break;
            }

            break;
        }
    }

    assert(input != nullptr);

    position += length;
    return length;
}


/*
 * istream handler
 *
 */

static size_t
parser_input_data(const void *data, size_t length, void *ctx)
{
    XmlParser *parser = (XmlParser *)ctx;

    const ScopePoolRef ref(*parser->pool TRACE_ARGS);
    return parser->Feed((const char *)data, length);
}

static void
parser_input_eof(void *ctx)
{
    XmlParser *parser = (XmlParser *)ctx;

    assert(parser->input != nullptr);

    parser->input = nullptr;
    parser->handler->eof(parser->handler_ctx, parser->position);
    pool_unref(parser->pool);
}

static void
parser_input_abort(GError *error, void *ctx)
{
    XmlParser *parser = (XmlParser *)ctx;

    assert(parser->input != nullptr);

    parser->input = nullptr;
    parser->handler->abort(error, parser->handler_ctx);
    pool_unref(parser->pool);
}

static const struct istream_handler parser_input_handler = {
    .data = parser_input_data,
    .eof = parser_input_eof,
    .abort = parser_input_abort,
};


/*
 * constructor
 *
 */

XmlParser *
parser_new(struct pool &pool, struct istream *input,
           const XmlParserHandler *handler, void *handler_ctx)
{
    auto parser = NewFromPool<XmlParser>(pool);

    assert(handler != nullptr);
    assert(handler->tag_start != nullptr);
    assert(handler->tag_finished != nullptr);
    assert(handler->attr_finished != nullptr);
    assert(handler->cdata != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->abort != nullptr);

    pool_ref(&pool);
    parser->pool = &pool;

    istream_assign_handler(&parser->input, input,
                           &parser_input_handler, parser,
                           0);

    parser->position = 0;
    parser->state = PARSER_NONE;
    parser->handler = handler;
    parser->handler_ctx = handler_ctx;
    parser->attr_value = expansible_buffer_new(&pool, 512, 8192);

    return parser;
}

void
parser_close(XmlParser *parser)
{
    assert(parser != nullptr);
    assert(parser->input != nullptr);

    istream_free_handler(&parser->input);
    pool_unref(parser->pool);
}

void
parser_read(XmlParser *parser)
{
    assert(parser != nullptr);
    assert(parser->input != nullptr);

    istream_read(parser->input);
}

void
parser_script(XmlParser *parser)
{
    assert(parser != nullptr);
    assert(parser->state == PARSER_NONE || parser->state == PARSER_INSIDE);

    parser->state = PARSER_SCRIPT;
}
