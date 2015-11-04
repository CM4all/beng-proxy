/*
 * Simple parser for CSS (Cascading Style Sheets).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_parser.hxx"
#include "css_syntax.hxx"
#include "pool.hxx"
#include "istream/istream_oo.hxx"
#include "istream/istream_pointer.hxx"
#include "util/CharUtil.hxx"
#include "util/TrivialArray.hxx"

enum CssParserState {
    CSS_PARSER_NONE,
    CSS_PARSER_BLOCK,
    CSS_PARSER_CLASS_NAME,
    CSS_PARSER_XML_ID,
    CSS_PARSER_DISCARD_QUOTED,
    CSS_PARSER_PROPERTY,
    CSS_PARSER_POST_PROPERTY,
    CSS_PARSER_PRE_VALUE,
    CSS_PARSER_VALUE,
    CSS_PARSER_PRE_URL,
    CSS_PARSER_URL,

    /**
     * An '@' was found.  Feeding characters into "name".
     */
    CSS_PARSER_AT,
    CSS_PARSER_PRE_IMPORT,
    CSS_PARSER_IMPORT,
};

struct CssParser {
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

    struct pool *pool;

    bool block;

    IstreamPointer input;
    off_t position;

    const CssParserHandler *handler;
    void *handler_ctx;

    /* internal state */
    CssParserState state;

    char quote;

    off_t name_start;
    StringBuffer<64> name_buffer;

    StringBuffer<64> value_buffer;

    off_t url_start;
    StringBuffer<1024> url_buffer;

    CssParser(struct pool &pool, Istream &input, bool block,
              const CssParserHandler &handler, void *handler_ctx);

    size_t Feed(const char *start, size_t length);

    /* istream handler */

    size_t OnData(const void *data, size_t length) {
        assert(input.IsDefined());

        const ScopePoolRef ref(*pool TRACE_ARGS);
        return Feed((const char *)data, length);
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        assert(input.IsDefined());

        input.Clear();
        handler->eof(handler_ctx, position);
        pool_unref(pool);
    }

    void OnError(GError *error) {
        assert(input.IsDefined());

        input.Clear();
        handler->error(error, handler_ctx);
        pool_unref(pool);
    }
};

static const char *
skip_whitespace(const char *p, const char *end)
{
    while (p < end && IsWhitespaceOrNull(*p))
        ++p;

    return p;
}

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
    assert(input.IsDefined());
    assert(start != nullptr);
    assert(length > 0);

    const char *buffer = start, *end = start + length, *p;
    size_t nbytes;
    CssParserValue url;

    while (buffer < end) {
        switch (state) {
        case CSS_PARSER_NONE:
            do {
                switch (*buffer) {
                case '{':
                    /* start of block */
                    state = CSS_PARSER_BLOCK;

                    if (handler->block != nullptr)
                        handler->block(handler_ctx);
                    break;

                case '.':
                    if (handler->class_name != nullptr) {
                        state = CSS_PARSER_CLASS_NAME;
                        name_start = position + (off_t)(buffer - start) + 1;
                        name_buffer.clear();
                    }

                    break;

                case '#':
                    if (handler->xml_id != nullptr) {
                        state = CSS_PARSER_XML_ID;
                        name_start = position + (off_t)(buffer - start) + 1;
                        name_buffer.clear();
                    }

                    break;

                case '@':
                    if (handler->import != nullptr) {
                        state = CSS_PARSER_AT;
                        name_buffer.clear();
                    }

                    break;
                }

                ++buffer;
            } while (buffer < end && state == CSS_PARSER_NONE);

            break;

        case CSS_PARSER_CLASS_NAME:
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

                    state = CSS_PARSER_NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case CSS_PARSER_XML_ID:
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

                    state = CSS_PARSER_NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case CSS_PARSER_BLOCK:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = CSS_PARSER_NONE;
                    break;

                case ':':
                    /* colon introduces property value */
                    state = CSS_PARSER_PRE_VALUE;
                    name_buffer.clear();
                    break;

                case '\'':
                case '"':
                    state = CSS_PARSER_DISCARD_QUOTED;
                    quote = *buffer;
                    break;

                default:
                    if (is_css_ident_start(*buffer) &&
                        handler->property_keyword != nullptr) {
                        state = CSS_PARSER_PROPERTY;
                        name_start = position + (off_t)(buffer - start);
                        name_buffer.clear();
                        name_buffer.push_back(*buffer);
                    }
                }

                ++buffer;
            } while (buffer < end && state == CSS_PARSER_BLOCK);
            break;

        case CSS_PARSER_DISCARD_QUOTED:
            p = (const char *)memchr(buffer, quote, end - buffer);
            if (p == nullptr) {
                nbytes = end - start;
                position += (off_t)nbytes;
                return nbytes;
            }

            state = CSS_PARSER_BLOCK;
            buffer = p + 1;
            break;

        case CSS_PARSER_PROPERTY:
            while (buffer < end) {
                if (!is_css_ident_char(*buffer)) {
                    state = CSS_PARSER_POST_PROPERTY;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            }

            break;

        case CSS_PARSER_POST_PROPERTY:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = CSS_PARSER_NONE;
                    break;

                case ':':
                    /* colon introduces property value */
                    state = CSS_PARSER_PRE_VALUE;
                    break;

                case '\'':
                case '"':
                    state = CSS_PARSER_DISCARD_QUOTED;
                    quote = *buffer;
                    break;
                }

                ++buffer;
            } while (buffer < end && state == CSS_PARSER_BLOCK);
            break;

        case CSS_PARSER_PRE_VALUE:
            buffer = skip_whitespace(buffer, end);
            if (buffer < end) {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = CSS_PARSER_NONE;
                    ++buffer;
                    break;

                case ';':
                    state = CSS_PARSER_BLOCK;
                    ++buffer;
                    break;

                default:
                    state = CSS_PARSER_VALUE;
                    value_buffer.clear();
                }
            }

            break;

        case CSS_PARSER_VALUE:
            do {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = CSS_PARSER_NONE;
                    break;

                case ';':
                    if (!name_buffer.empty()) {
                        assert(handler->property_keyword != nullptr);

                        name_buffer.push_back('\0');
                        value_buffer.push_back('\0');

                        handler->property_keyword(name_buffer.raw(),
                                                  value_buffer.raw(),
                                                  name_start,
                                                  position + (off_t)(buffer - start) + 1,
                                                  handler_ctx);
                    }

                    state = CSS_PARSER_BLOCK;
                    break;

                case '\'':
                case '"':
                    state = CSS_PARSER_DISCARD_QUOTED;
                    quote = *buffer;
                    break;

                default:
                    if (value_buffer.size() >= value_buffer.capacity() - 1)
                        break;

                    value_buffer.push_back(*buffer);
                    if (handler->url != nullptr &&
                        at_url_start(value_buffer.raw(),
                                     value_buffer.size()))
                        state = CSS_PARSER_PRE_URL;
                }

                ++buffer;
            } while (buffer < end && state == CSS_PARSER_VALUE);
            break;

        case CSS_PARSER_PRE_URL:
            buffer = skip_whitespace(buffer, end);
            if (buffer < end) {
                switch (*buffer) {
                case '}':
                    /* end of block */
                    if (block)
                        break;

                    state = CSS_PARSER_NONE;
                    ++buffer;
                    break;

                case '\'':
                case '"':
                    state = CSS_PARSER_URL;
                    quote = *buffer++;
                    url_start = position + (off_t)(buffer - start);
                    url_buffer.clear();
                    break;

                default:
                    state = CSS_PARSER_BLOCK;
                }
            }

            break;

        case CSS_PARSER_URL:
            p = (const char *)memchr(buffer, quote, end - buffer);
            if (p == nullptr) {
                nbytes = end - start;
                url_buffer.AppendTruncated({buffer, nbytes});
                position += (off_t)nbytes;
                return nbytes;
            }

            /* found the end of the URL - copy the rest, and invoke
               the handler method "url()" */
            nbytes = p - buffer;
            url_buffer.AppendTruncated({buffer, nbytes});

            buffer = p + 1;
            state = CSS_PARSER_BLOCK;

            url.start = url_start;
            url.end = position + (off_t)(p - start);
            url.value = url_buffer;
            handler->url(&url, handler_ctx);
            if (!input.IsDefined())
                return 0;

            break;

        case CSS_PARSER_AT:
            do {
                if (!is_css_nmchar(*buffer)) {
                    if (name_buffer.EqualsLiteral("import"))
                        state = CSS_PARSER_PRE_IMPORT;
                    else
                        state = CSS_PARSER_NONE;
                    break;
                }

                if (name_buffer.size() < name_buffer.capacity() - 1)
                    name_buffer.push_back(*buffer);

                ++buffer;
            } while (buffer < end);

            break;

        case CSS_PARSER_PRE_IMPORT:
            do {
                if (!IsWhitespaceOrNull(*buffer)) {
                    if (*buffer == '"') {
                        ++buffer;
                        state = CSS_PARSER_IMPORT;
                        url_start = position + (off_t)(buffer - start);
                        url_buffer.clear();
                    } else
                        state = CSS_PARSER_NONE;
                    break;
                }

                ++buffer;
            } while (buffer < end);

            break;

        case CSS_PARSER_IMPORT:
            p = (const char *)memchr(buffer, '"', end - buffer);
            if (p == nullptr) {
                nbytes = end - start;
                url_buffer.AppendTruncated({buffer, nbytes});
                position += (off_t)nbytes;
                return nbytes;
            }

            /* found the end of the URL - copy the rest, and invoke
               the handler method "import()" */
            nbytes = p - buffer;
            url_buffer.AppendTruncated({buffer, nbytes});

            buffer = p + 1;
            state = CSS_PARSER_NONE;

            url.start = url_start;
            url.end = position + (off_t)(p - start);
            url.value = url_buffer;
            handler->import(&url, handler_ctx);
            if (!input.IsDefined())
                return 0;

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

CssParser::CssParser(struct pool &_pool, Istream &_input, bool _block,
                     const CssParserHandler &_handler,
                     void *_handler_ctx)
    :pool(&_pool), block(_block),
     input(_input, MakeIstreamHandler<CssParser>::handler, this),
     position(0),
     handler(&_handler), handler_ctx(_handler_ctx),
     state(block ? CSS_PARSER_BLOCK : CSS_PARSER_NONE)
{
}

CssParser *
css_parser_new(struct pool &pool, Istream &input, bool block,
               const CssParserHandler &handler, void *handler_ctx)
{
    assert(handler.eof != nullptr);
    assert(handler.error != nullptr);

    pool_ref(&pool);

    return NewFromPool<CssParser>(pool, pool, input, block,
                                  handler, handler_ctx);
}

void
css_parser_close(CssParser *parser)
{
    assert(parser != nullptr);
    assert(parser->input.IsDefined());

    parser->input.Close();
    pool_unref(parser->pool);
}

void
css_parser_read(CssParser *parser)
{
    assert(parser != nullptr);
    assert(parser->input.IsDefined());

    parser->input.Read();
}
