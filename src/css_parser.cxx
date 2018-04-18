/*
 * Copyright 2007-2018 Content Management AG
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

#include "css_parser.hxx"
#include "css_syntax.hxx"
#include "pool/pool.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringUtil.hxx"
#include "util/TrivialArray.hxx"

struct CssParser final : PoolHolder, IstreamSink, DestructAnchor {
    template<size_t max>
    class StringBuffer : public TrivialArray<char, max> {
    public:
        using TrivialArray<char, max>::capacity;
        using TrivialArray<char, max>::size;
        using TrivialArray<char, max>::raw;
        using TrivialArray<char, max>::end;

        size_t GetRemainingSpace() const {
            return capacity() - size();
        }

        void AppendTruncated(StringView p) {
            size_t n = std::min(p.size, GetRemainingSpace());
            std::copy_n(p.data, n, end());
            this->the_size += n;
        }

        constexpr operator StringView() const {
            return {raw(), size()};
        }

        gcc_pure
        bool Equals(StringView other) const {
            return other.Equals(*this);
        }

        template<size_t n>
        bool EqualsLiteral(const char (&value)[n]) const {
            return Equals({value, n - 1});
        }
    };

    bool block;

    off_t position;

    const CssParserHandler *handler;
    void *handler_ctx;

    /* internal state */
    enum class State {
        NONE,
        BLOCK,
        CLASS_NAME,
        XML_ID,
        DISCARD_QUOTED,
        PROPERTY,
        POST_PROPERTY,
        PRE_VALUE,
        VALUE,
        PRE_URL,
        URL,

        /**
         * An '@' was found.  Feeding characters into "name".
         */
        AT,
        PRE_IMPORT,
        IMPORT,
    } state;

    char quote;

    off_t name_start;
    StringBuffer<64> name_buffer;

    StringBuffer<64> value_buffer;

    off_t url_start;
    StringBuffer<1024> url_buffer;

    CssParser(struct pool &pool, UnusedIstreamPtr input, bool block,
              const CssParserHandler &handler, void *handler_ctx);

    void Destroy() {
        this->~CssParser();
    }

    void Read() {
        input.Read();
    }

    void Close() {
        input.Close();
    }

    size_t Feed(const char *start, size_t length);

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        return Feed((const char *)data, length);
    }

    void OnEof() noexcept override {
        input.Clear();
        handler->eof(handler_ctx, position);
        Destroy();
    }

    void OnError(std::exception_ptr ep) noexcept override {
        input.Clear();
        handler->error(ep, handler_ctx);
        Destroy();
    }
};

gcc_pure
static bool
at_url_start(const char *p, size_t length)
{
    return length >= 4 && memcmp(p + length - 4, "url(", 4) == 0 &&
        (/* just url(): */ length == 4 ||
         /* url() after another token: */
         IsWhitespaceOrNull(p[length - 5]));
}

size_t
CssParser::Feed(const char *start, size_t length)
{
    assert(start != nullptr);
    assert(length > 0);

    const char *buffer = start, *end = start + length, *p;

    while (buffer < end) {
        switch (state) {
        case State::NONE:
            do {
                switch (*buffer) {
                case '{':
                    /* start of block */
                    state = State::BLOCK;

                    if (handler->block != nullptr)
                        handler->block(handler_ctx);
                    break;

                case '.':
                    if (handler->class_name != nullptr) {
                        state = State::CLASS_NAME;
                        name_start = position + (off_t)(buffer - start) + 1;
                        name_buffer.clear();
                    }

                    break;

                case '#':
                    if (handler->xml_id != nullptr) {
                        state = State::XML_ID;
                        name_start = position + (off_t)(buffer - start) + 1;
                        name_buffer.clear();
                    }

                    break;

                case '@':
                    if (handler->import != nullptr) {
                        state = State::AT;
                        name_buffer.clear();
                    }

                    break;
                }

                ++buffer;
            } while (buffer < end && state == State::NONE);

            break;

        case State::CLASS_NAME:
            do {
                if (!is_css_nmchar(*buffer)) {
                    if (!name_buffer.empty()) {
                        CssParserValue name = {
                            .start = name_start,
                            .end = position + (off_t)(buffer - start),
                        };

                        name.value = name_buffer;
                        handler->class_name(&name,handler_ctx);
                    }

                    state = State::NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case State::XML_ID:
            do {
                if (!is_css_nmchar(*buffer)) {
                    if (!name_buffer.empty()) {
                        CssParserValue name = {
                            .start = name_start,
                            .end = position + (off_t)(buffer - start),
                        };

                        name.value = name_buffer;
                        handler->xml_id(&name, handler_ctx);
                    }

                    state = State::NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case State::BLOCK:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = State::NONE;
                    break;

                case ':':
                    /* colon introduces property value */
                    state = State::PRE_VALUE;
                    name_buffer.clear();
                    break;

                case '\'':
                case '"':
                    state = State::DISCARD_QUOTED;
                    quote = *buffer;
                    break;

                default:
                    if (is_css_ident_start(*buffer) &&
                        handler->property_keyword != nullptr) {
                        state = State::PROPERTY;
                        name_start = position + (off_t)(buffer - start);
                        name_buffer.clear();
                        name_buffer.push_back(*buffer);
                    }
                }

                ++buffer;
            } while (buffer < end && state == State::BLOCK);
            break;

        case State::DISCARD_QUOTED:
            p = (const char *)memchr(buffer, quote, end - buffer);
            if (p == nullptr) {
                size_t nbytes = end - start;
                position += (off_t)nbytes;
                return nbytes;
            }

            state = State::BLOCK;
            buffer = p + 1;
            break;

        case State::PROPERTY:
            while (buffer < end) {
                if (!is_css_ident_char(*buffer)) {
                    state = State::POST_PROPERTY;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            }

            break;

        case State::POST_PROPERTY:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = State::NONE;
                    break;

                case ':':
                    /* colon introduces property value */
                    state = State::PRE_VALUE;
                    break;

                case '\'':
                case '"':
                    state = State::DISCARD_QUOTED;
                    quote = *buffer;
                    break;
                }

                ++buffer;
            } while (buffer < end && state == State::BLOCK);
            break;

        case State::PRE_VALUE:
            buffer = StripLeft(buffer, end);
            if (buffer < end) {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = State::NONE;
                    ++buffer;
                    break;

                case ';':
                    state = State::BLOCK;
                    ++buffer;
                    break;

                default:
                    state = State::VALUE;
                    value_buffer.clear();
                }
            }

            break;

        case State::VALUE:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = State::NONE;
                    break;

                case ';':
                    if (!name_buffer.empty()) {
                        assert(handler->property_keyword != nullptr);

                        name_buffer.push_back('\0');

                        handler->property_keyword(name_buffer.raw(),
                                                  value_buffer,
                                                  name_start,
                                                  position + (off_t)(buffer - start) + 1,
                                                  handler_ctx);
                    }

                    state = State::BLOCK;
                    break;

                case '\'':
                case '"':
                    state = State::DISCARD_QUOTED;
                    quote = *buffer;
                    break;

                default:
                    if (value_buffer.size() >= value_buffer.capacity() - 1)
                        break;

                    value_buffer.push_back(*buffer);
                    if (handler->url != nullptr &&
                        at_url_start(value_buffer.raw(),
                                     value_buffer.size()))
                        state = State::PRE_URL;
                }

                ++buffer;
            } while (buffer < end && state == State::VALUE);
            break;

        case State::PRE_URL:
            buffer = StripLeft(buffer, end);
            if (buffer < end) {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = State::NONE;
                    ++buffer;
                    break;

                case '\'':
                case '"':
                    state = State::URL;
                    quote = *buffer++;
                    url_start = position + (off_t)(buffer - start);
                    url_buffer.clear();
                    break;

                default:
                    state = State::BLOCK;
                }
            }

            break;

        case State::URL:
            p = (const char *)memchr(buffer, quote, end - buffer);
            if (p == nullptr) {
                size_t nbytes = end - start;
                url_buffer.AppendTruncated({buffer, nbytes});
                position += (off_t)nbytes;
                return nbytes;
            }

            {
                /* found the end of the URL - copy the rest, and
                   invoke the handler method "url()" */
                size_t nbytes = p - buffer;
                url_buffer.AppendTruncated({buffer, nbytes});

                buffer = p + 1;
                state = State::BLOCK;

                CssParserValue url;
                url.start = url_start;
                url.end = position + (off_t)(p - start);
                url.value = url_buffer;

                const DestructObserver destructed(*this);
                handler->url(&url, handler_ctx);
                if (destructed)
                    return 0;
            }

            break;

        case State::AT:
            do {
                if (!is_css_nmchar(*buffer)) {
                    if (name_buffer.EqualsLiteral("import"))
                        state = State::PRE_IMPORT;
                    else
                        state = State::NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case State::PRE_IMPORT:
            do {
                if (!IsWhitespaceOrNull(*buffer)) {
                    if (*buffer == '"') {
                        ++buffer;
                        state = State::IMPORT;
                        url_start = position + (off_t)(buffer - start);
                        url_buffer.clear();
                    } else
                        state = State::NONE;
                    break;
                }

                ++buffer;
            } while (buffer < end);

            break;

        case State::IMPORT:
            p = (const char *)memchr(buffer, '"', end - buffer);
            if (p == nullptr) {
                size_t nbytes = end - start;
                url_buffer.AppendTruncated({buffer, nbytes});
                position += (off_t)nbytes;
                return nbytes;
            }

            {
                /* found the end of the URL - copy the rest, and
                   invoke the handler method "import()" */
                size_t nbytes = p - buffer;
                url_buffer.AppendTruncated({buffer, nbytes});

                buffer = p + 1;
                state = State::NONE;

                CssParserValue url;
                url.start = url_start;
                url.end = position + (off_t)(p - start);
                url.value = url_buffer;

                const DestructObserver destructed(*this);
                handler->import(&url, handler_ctx);
                if (destructed)
                    return 0;
            }

            break;
        }
    }

    position += length;
    return length;
}

/*
 * constructor
 *
 */

CssParser::CssParser(struct pool &_pool, UnusedIstreamPtr _input, bool _block,
                     const CssParserHandler &_handler,
                     void *_handler_ctx)
    :PoolHolder(_pool), IstreamSink(std::move(_input)), block(_block),
     position(0),
     handler(&_handler), handler_ctx(_handler_ctx),
     state(block ? State::BLOCK : State::NONE)
{
}

CssParser *
css_parser_new(struct pool &pool, UnusedIstreamPtr input, bool block,
               const CssParserHandler &handler, void *handler_ctx)
{
    assert(handler.eof != nullptr);
    assert(handler.error != nullptr);

    return NewFromPool<CssParser>(pool, pool, std::move(input), block,
                                  handler, handler_ctx);
}

void
css_parser_close(CssParser *parser)
{
    assert(parser != nullptr);

    parser->Close();
    parser->Destroy();
}

void
css_parser_read(CssParser *parser)
{
    assert(parser != nullptr);

    parser->Read();
}
