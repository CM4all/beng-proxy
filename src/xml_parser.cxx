/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "xml_parser.hxx"
#include "pool/pool.hxx"
#include "html_chars.hxx"
#include "expansible_buffer.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/CharUtil.hxx"
#include "util/DestructObserver.hxx"
#include "util/Poison.hxx"

#include <assert.h>
#include <string.h>

class XmlParser final : IstreamSink, DestructAnchor {
    struct pool *pool;

    off_t position = 0;

    /* internal state */
    enum class State {
        NONE,

        /** within a SCRIPT element; only accept "</" to break out */
        SCRIPT,

        /** found '<' within a SCRIPT element */
        SCRIPT_ELEMENT_NAME,

        /** parsing an element name */
        ELEMENT_NAME,

        /** inside the element tag */
        ELEMENT_TAG,

        /** inside the element tag, but ignore attributes */
        ELEMENT_BORING,

        /** parsing attribute name */
        ATTR_NAME,

        /** after the attribute name, waiting for '=' */
        AFTER_ATTR_NAME,

        /** after the '=', waiting for the attribute value */
        BEFORE_ATTR_VALUE,

        /** parsing the quoted attribute value */
        ATTR_VALUE,

        /** compatibility with older and broken HTML: attribute value
            without quotes */
        ATTR_VALUE_COMPAT,

        /** found a slash, waiting for the '>' */
        SHORT,

        /** inside the element, currently unused */
        INSIDE,

        /** parsing a declaration name beginning with "<!" */
        DECLARATION_NAME,

        /** within a CDATA section */
        CDATA_SECTION,

        /** within a comment */
        COMMENT,
    } state = State::NONE;

    /* element */
    XmlParserTag tag;
    char tag_name[64];
    size_t tag_name_length;

    /* attribute */
    char attr_name[64];
    size_t attr_name_length;
    char attr_value_delimiter;
    ExpansibleBuffer attr_value;
    XmlParserAttribute attr;

    /** in a CDATA section, how many characters have been matching
        CDEnd ("]]>")? */
    size_t cdend_match;

    /** in a comment, how many consecutive minus are there? */
    unsigned minus_count;

    XmlParserHandler &handler;

public:
    XmlParser(struct pool &_pool, UnusedIstreamPtr _input,
              XmlParserHandler &_handler) noexcept
        :IstreamSink(std::move(_input)), pool(&_pool),
         attr_value(*pool, 512, 8192),
         handler(_handler) {
        pool_ref(pool);
    }

    void Destroy() noexcept {
        DeleteUnrefPool(*pool, this);
    }

    void Close() noexcept {
        assert(input.IsDefined());

        ClearAndCloseInput();
        Destroy();
    }

    bool Read() noexcept {
        assert(input.IsDefined());

        const DestructObserver destructed(*this);
        input.Read();
        return !destructed;
    }

    void Script() noexcept {
        assert(state == State::NONE ||
               state == State::INSIDE);

        state = State::SCRIPT;
    }

private:
    void InvokeAttributeFinished() noexcept {
        attr.name = {attr_name, attr_name_length};
        attr.value = attr_value.ReadStringView();

        handler.OnXmlAttributeFinished(attr);
        PoisonUndefinedT(attr);
    }

    size_t Feed(const char *start, size_t length) noexcept;

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) noexcept override {
        return Feed((const char *)data, length);
    }

    void OnEof() noexcept override {
        assert(input.IsDefined());

        input.Clear();
        handler.OnXmlEof(position);
        Destroy();
    }

    void OnError(std::exception_ptr ep) noexcept override {
        assert(input.IsDefined());

        input.Clear();
        handler.OnXmlError(ep);
        Destroy();
    }
};

inline size_t
XmlParser::Feed(const char *start, size_t length) noexcept
{
    const char *buffer = start, *end = start + length, *p;
    size_t nbytes;

    assert(input.IsDefined());
    assert(buffer != nullptr);
    assert(length > 0);

    while (buffer < end) {
        switch (state) {
        case State::NONE:
        case State::SCRIPT:
            /* find first character */
            p = (const char *)memchr(buffer, '<', end - buffer);
            if (p == nullptr) {
                nbytes = handler.OnXmlCdata(buffer, end - buffer, true,
                                            position + buffer - start);
                assert(nbytes <= (size_t)(end - buffer));

                nbytes += buffer - start;
                position += (off_t)nbytes;
                return nbytes;
            }

            if (p > buffer) {
                nbytes = handler.OnXmlCdata(buffer, p - buffer, true,
                                            position + buffer - start);
                assert(nbytes <= (size_t)(p - buffer));

                if (nbytes < (size_t)(p - buffer)) {
                    nbytes += buffer - start;
                    position += (off_t)nbytes;
                    return nbytes;
                }
            }

            tag.start = position + (off_t)(p - start);
            state = state == State::NONE
                ? State::ELEMENT_NAME
                : State::SCRIPT_ELEMENT_NAME;
            tag_name_length = 0;
            tag.type = XmlParserTagType::OPEN;
            buffer = p + 1;
            break;

        case State::SCRIPT_ELEMENT_NAME:
            if (*buffer == '/') {
                state = State::ELEMENT_NAME;
                tag.type = XmlParserTagType::CLOSE;
                ++buffer;
            } else {
                nbytes = handler.OnXmlCdata("<", 1, true,
                                            position + buffer - start);
                assert(nbytes <= (size_t)(end - buffer));

                if (nbytes == 0) {
                    nbytes = buffer - start;
                    position += nbytes;
                    return nbytes;
                }

                state = State::SCRIPT;
            }

            break;

        case State::ELEMENT_NAME:
            /* copy element name */
            while (buffer < end) {
                if (is_html_name_char(*buffer)) {
                    if (tag_name_length == sizeof(tag_name)) {
                        /* name buffer overflowing */
                        state = State::NONE;
                        break;
                    }

                    tag_name[tag_name_length++] = ToLowerASCII(*buffer++);
                } else if (*buffer == '/' && tag_name_length == 0) {
                    tag.type = XmlParserTagType::CLOSE;
                    ++buffer;
                } else if (*buffer == '?' && tag_name_length == 0) {
                    /* start of processing instruction */
                    tag.type = XmlParserTagType::PI;
                    ++buffer;
                } else if ((IsWhitespaceOrNull(*buffer) || *buffer == '/' ||
                            *buffer == '?' || *buffer == '>') &&
                           tag_name_length > 0) {
                    bool interesting;

                    tag.name = {tag_name, tag_name_length};

                    interesting = handler.OnXmlTagStart(tag);

                    state = interesting ? State::ELEMENT_TAG : State::ELEMENT_BORING;
                    break;
                } else if (*buffer == '!' && tag_name_length == 0) {
                    state = State::DECLARATION_NAME;
                    ++buffer;
                    break;
                } else {
                    state = State::NONE;
                    break;
                }
            }

            break;

        case State::ELEMENT_TAG:
            do {
                if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else if (*buffer == '/' && tag.type == XmlParserTagType::OPEN) {
                    tag.type = XmlParserTagType::SHORT;
                    state = State::SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '?' && tag.type == XmlParserTagType::PI) {
                    state = State::SHORT;
                    ++buffer;
                    break;
                } else if (*buffer == '>') {
                    state = State::INSIDE;
                    ++buffer;
                    tag.end = position + (off_t)(buffer - start);

                    if (!handler.OnXmlTagFinished(tag))
                        return 0;

                    PoisonUndefinedT(tag);
                    break;
                } else if (is_html_name_start_char(*buffer)) {
                    state = State::ATTR_NAME;
                    attr.name_start = position + (off_t)(buffer - start);
                    attr_name_length = 0;
                    attr_value.Clear();
                    break;
                } else {
                    /* ignore this syntax error and just close the
                       element tag */

                    tag.end = position + (off_t)(buffer - start);
                    state = State::INSIDE;

                    if (!handler.OnXmlTagFinished(tag))
                        return 0;

                    state = State::NONE;
                    break;
                }
            } while (buffer < end);

            break;

        case State::ELEMENT_BORING:
            /* ignore this tag */

            p = (const char *)memchr(buffer, '>', end - buffer);
            if (p != nullptr) {
                /* the "boring" tag has been closed */
                buffer = p + 1;
                state = State::NONE;
            } else
                buffer = end;
            break;

        case State::ATTR_NAME:
            /* copy attribute name */
            do {
                if (is_html_name_char(*buffer)) {
                    if (attr_name_length == sizeof(attr_name)) {
                        /* name buffer overflowing */
                        state = State::ELEMENT_TAG;
                        break;
                    }

                    attr_name[attr_name_length++] = ToLowerASCII(*buffer++);
                } else if (*buffer == '=' || IsWhitespaceOrNull(*buffer)) {
                    state = State::AFTER_ATTR_NAME;
                    break;
                } else {
                    InvokeAttributeFinished();
                    state = State::ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case State::AFTER_ATTR_NAME:
            /* wait till we find '=' */
            do {
                if (*buffer == '=') {
                    state = State::BEFORE_ATTR_VALUE;
                    ++buffer;
                    break;
                } else if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else {
                    InvokeAttributeFinished();
                    state = State::ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case State::BEFORE_ATTR_VALUE:
            do {
                if (*buffer == '"' || *buffer == '\'') {
                    state = State::ATTR_VALUE;
                    attr_value_delimiter = *buffer;
                    ++buffer;
                    attr.value_start = position + (off_t)(buffer - start);
                    break;
                } else if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else {
                    state = State::ATTR_VALUE_COMPAT;
                    attr.value_start = position + (off_t)(buffer - start);
                    break;
                }
            } while (buffer < end);

            break;

        case State::ATTR_VALUE:
            /* wait till we find the delimiter */
            p = (const char *)memchr(buffer, attr_value_delimiter,
                                     end - buffer);
            if (p == nullptr) {
                if (!attr_value.Write(buffer, end - buffer)) {
                    state = State::ELEMENT_TAG;
                    break;
                }

                buffer = end;
            } else {
                if (!attr_value.Write(buffer, p - buffer)) {
                    state = State::ELEMENT_TAG;
                    break;
                }

                buffer = p + 1;
                attr.end = position + (off_t)(buffer - start);
                attr.value_end = attr.end - 1;
                InvokeAttributeFinished();
                state = State::ELEMENT_TAG;
            }

            break;

        case State::ATTR_VALUE_COMPAT:
            /* wait till the value is finished */
            do {
                if (!IsWhitespaceOrNull(*buffer) && *buffer != '>') {
                    if (!attr_value.Write(buffer, 1)) {
                        state = State::ELEMENT_TAG;
                        break;
                    }

                    ++buffer;
                } else {
                    attr.value_end = attr.end =
                        position + (off_t)(buffer - start);
                    InvokeAttributeFinished();
                    state = State::ELEMENT_TAG;
                    break;
                }
            } while (buffer < end);

            break;

        case State::SHORT:
            do {
                if (IsWhitespaceOrNull(*buffer)) {
                    ++buffer;
                } else if (*buffer == '>') {
                    state = State::NONE;
                    ++buffer;
                    tag.end = position + (off_t)(buffer - start);

                    if (!handler.OnXmlTagFinished(tag))
                        return 0;

                    PoisonUndefinedT(tag);

                    break;
                } else {
                    /* ignore this syntax error and just close the
                       element tag */

                    tag.end = position + (off_t)(buffer - start);
                    state = State::INSIDE;

                    if (!handler.OnXmlTagFinished(tag))
                        return 0;

                    PoisonUndefinedT(tag);
                    state = State::NONE;

                    break;
                }
            } while (buffer < end);

            break;

        case State::INSIDE:
            /* XXX */
            state = State::NONE;
            break;

        case State::DECLARATION_NAME:
            /* copy declaration element name */
            while (buffer < end) {
                if (IsAlphaNumericASCII(*buffer) || *buffer == ':' ||
                    *buffer == '-' || *buffer == '_' || *buffer == '[') {
                    if (tag_name_length == sizeof(tag_name)) {
                        /* name buffer overflowing */
                        state = State::NONE;
                        break;
                    }

                    tag_name[tag_name_length++] = ToLowerASCII(*buffer++);

                    if (tag_name_length == 7 &&
                        memcmp(tag_name, "[cdata[", 7) == 0) {
                        state = State::CDATA_SECTION;
                        cdend_match = 0;
                        break;
                    }

                    if (tag_name_length == 2 &&
                        memcmp(tag_name, "--", 2) == 0) {
                        state = State::COMMENT;
                        minus_count = 0;
                        break;
                    }
                } else {
                    state = State::NONE;
                    break;
                }
            }

            break;

        case State::CDATA_SECTION:
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

                        nbytes = handler.OnXmlCdata(p, cdata_length, false,
                                                    cdata_start);
                        assert(nbytes <= (size_t)(buffer - p));

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
                    state = State::NONE;
                    break;
                } else {
                    if (cdend_match > 0) {
                        /* we had a partial match, and now we have to
                           restore the data we already skipped */
                        assert(cdend_match < 3);

                        nbytes = handler.OnXmlCdata("]]", cdend_match, false,
                                                    position + buffer - start);
                        assert(nbytes <= cdend_match);

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

                nbytes = handler.OnXmlCdata(p, cdata_length, false,
                                            cdata_start);
                assert(nbytes <= (size_t)(buffer - p));

                if (nbytes < (size_t)(buffer - p)) {
                    nbytes += p - start;
                    position += (off_t)nbytes;
                    return nbytes;
                }
            }

            break;

        case State::COMMENT:
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
                    state = State::NONE;
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

    assert(input.IsDefined());

    position += length;
    return length;
}


/*
 * constructor
 *
 */

XmlParser *
parser_new(struct pool &pool, UnusedIstreamPtr input,
           XmlParserHandler &handler) noexcept
{
    return NewFromPool<XmlParser>(pool, pool, std::move(input), handler);
}

void
parser_close(XmlParser *parser) noexcept
{
    assert(parser != nullptr);

    parser->Close();
}

bool
parser_read(XmlParser *parser) noexcept
{
    assert(parser != nullptr);

    return parser->Read();
}

void
parser_script(XmlParser *parser) noexcept
{
    assert(parser != nullptr);

    parser->Script();
}
