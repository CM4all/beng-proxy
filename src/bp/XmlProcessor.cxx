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

#include "XmlProcessor.hxx"
#include "TextProcessor.hxx"
#include "CssProcessor.hxx"
#include "CssRewrite.hxx"
#include "Global.hxx"
#include "penv.hxx"
#include "xml_parser.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "widget/Widget.hxx"
#include "widget/Approval.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/Class.hxx"
#include "widget/Error.hxx"
#include "widget/Inline.hxx"
#include "widget/RewriteUri.hxx"
#include "pool/tpool.hxx"
#include "expansible_buffer.hxx"
#include "escape_class.hxx"
#include "escape_html.hxx"
#include "strmap.hxx"
#include "css_syntax.hxx"
#include "css_util.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream.hxx"
#include "istream/istream.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/istream_catch.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_tee.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "util/CharUtil.hxx"
#include "util/Macros.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"
#include "util/Cancellable.hxx"
#include "util/ScopeExit.hxx"

#include <assert.h>
#include <string.h>

enum uri_base {
    URI_BASE_TEMPLATE,
    URI_BASE_WIDGET,
    URI_BASE_CHILD,
    URI_BASE_PARENT,
};

struct uri_rewrite {
    enum uri_base base;
    RewriteUriMode mode;

    char view[64];
};

struct XmlProcessor final : XmlParserHandler, Cancellable, PoolLeakDetector {
    class CdataIstream final : public Istream {
        friend struct XmlProcessor;
        XmlProcessor &processor;

    public:
        explicit CdataIstream(XmlProcessor &_processor)
            :Istream(_processor.pool), processor(_processor) {}

        /* virtual methods from class Istream */
        void _Read() noexcept override;
        void _Close() noexcept override;
    };

    struct pool &pool, &caller_pool;

    Widget &container;
    const char *lookup_id;
    struct processor_env &env;
    const unsigned options;

    SharedPoolPtr<ReplaceIstreamControl> replace;

    XmlParser *parser;
    bool had_input;

    enum class Tag {
        NONE,
        IGNORE,
        OTHER,
        WIDGET,
        WIDGET_PATH_INFO,
        WIDGET_PARAM,
        WIDGET_HEADER,
        WIDGET_VIEW,
        A,
        FORM,
        IMG,
        SCRIPT,
        PARAM,
        REWRITE_URI,

        /**
         * The "meta" element.  This may morph into #META_REFRESH when
         * an http-equiv="refresh" attribute is found.
         */
        META,

        META_REFRESH,

        /**
         * A "meta" element whose "content" attribute contains a URL
         * to be rewritten, e.g. <meta property="og:image" content="...">
         */
        META_URI_CONTENT,

        /**
         * The "style" element.  This value later morphs into
         * #STYLE_PROCESS if #PROCESSOR_STYLE is enabled.
         */
        STYLE,

        /**
         * Only used when #PROCESSOR_STYLE is enabled.  If active, then
         * CDATA is being fed into the CSS processor.
         */
        STYLE_PROCESS,
    };

    Tag tag = Tag::NONE;

    struct uri_rewrite uri_rewrite;

    /**
     * The default value for #uri_rewrite.
     */
    struct uri_rewrite default_uri_rewrite;

    /**
     * A buffer that may be used for various temporary purposes
     * (e.g. attribute transformation).
     */
    ExpansibleBuffer buffer;

    /**
     * These values are used to buffer c:mode/c:base values in any
     * order, even after the actual URI attribute.
     */
    struct PostponedRewrite {
        bool pending = false;

        off_t uri_start, uri_end;
        ExpansibleBuffer value;

        /**
         * The positions of the c:mode/c:base attributes after the URI
         * attribute.  These have to be deleted *after* the URI
         * attribute has been rewritten.
         */
        struct {
            off_t start, end;
        } delete_[4];

        PostponedRewrite(struct pool &_pool) noexcept
            :value(_pool, 1024, 8192) {}
    } postponed_rewrite;

    struct CurrentWidget {
        off_t start_offset;

        struct pool &pool;
        Widget *widget = nullptr;

        struct Param {
            ExpansibleBuffer name;
            ExpansibleBuffer value;

            Param(struct pool &_pool)
                :name(_pool, 128, 512),
                 value(_pool, 512, 4096) {}
        } param;

        ExpansibleBuffer params;

        CurrentWidget(struct pool &widget_pool,
                      struct pool &processor_pool) noexcept
            :pool(widget_pool), param(processor_pool),
             params(processor_pool, 1024, 8192) {}
    } widget;

    /**
     * Only valid if #cdata_stream_active is true.
     */
    off_t cdata_start;
    CdataIstream *cdata_istream;

    WidgetLookupHandler *handler;

    CancellablePointer *cancel_ptr;

    XmlProcessor(struct pool &_pool, struct pool &_caller_pool,
                 Widget &_widget, struct processor_env &_env,
                 unsigned _options) noexcept
        :PoolLeakDetector(_pool),
         pool(_pool), caller_pool(_caller_pool),
         container(_widget),
         env(_env), options(_options),
         buffer(pool, 128, 2048),
         postponed_rewrite(pool),
         widget(_widget.pool, pool) {
        pool_ref(&container.pool);
    }

    void InitParser(UnusedIstreamPtr input) noexcept {
        parser = parser_new(pool, std::move(input), *this);
    }

    bool IsQuiet() const noexcept {
        return !replace;
    }

    bool HasOptionRewriteUrl() const noexcept {
        return (options & PROCESSOR_REWRITE_URL) != 0;
    }

private:
    bool HasOptionPrefixClass() const noexcept {
        return (options & PROCESSOR_PREFIX_CSS_CLASS) != 0;
    }

    bool HasOptionPrefixId() const noexcept {
        return (options & PROCESSOR_PREFIX_XML_ID) != 0;
    }

    bool HasOptionPrefixAny() const noexcept {
        return (options & (PROCESSOR_PREFIX_CSS_CLASS|PROCESSOR_PREFIX_XML_ID)) != 0;
    }

    bool HasOptionStyle() const noexcept {
        return (options & PROCESSOR_STYLE) != 0;
    }

    void Destroy() noexcept {
        DeleteFromPool(pool, this);
    }

    void Close() noexcept {
        auto *parser2 = parser;
        Destroy();
        parser_close(parser2);
    }

    void Replace(off_t start, off_t end, UnusedIstreamPtr istream) noexcept {
        replace->Add(start, end, std::move(istream));
    }

    void ReplaceAttributeValue(const XmlParserAttribute &attr,
                               UnusedIstreamPtr value) noexcept {
        Replace(attr.value_start, attr.value_end, std::move(value));
    }

    void InitUriRewrite(Tag _tag) noexcept {
        assert(!postponed_rewrite.pending);

        tag = _tag;
        uri_rewrite = default_uri_rewrite;
    }

    void PostponeUriRewrite(off_t start, off_t end,
                            const void *value, size_t length) noexcept;

    void PostponeUriRewrite(const XmlParserAttribute &attr) noexcept {
        PostponeUriRewrite(attr.value_start, attr.value_end,
                           attr.value.data, attr.value.size);
    }

    void PostponeRefreshRewrite(const XmlParserAttribute &attr) noexcept;

    void CommitUriRewrite() noexcept;

    void DeleteUriRewrite(off_t start, off_t end) noexcept;

    void TransformUriAttribute(const XmlParserAttribute &attr,
                               enum uri_base base, RewriteUriMode mode,
                               const char *view) noexcept;

    bool LinkAttributeFinished(const XmlParserAttribute &attr) noexcept;
    void HandleClassAttribute(const XmlParserAttribute &attr) noexcept;
    void HandleIdAttribute(const XmlParserAttribute &attr) noexcept;
    void HandleStyleAttribute(const XmlParserAttribute &attr) noexcept;

    bool OnProcessingInstruction(StringView name) noexcept;
    bool OnStartElementInWidget(XmlParserTagType type,
                                StringView name) noexcept;

    /**
     * Throws an exception if the widget is not allowed here.
     */
    void PrepareEmbedWidget(Widget &child_widget);

    UnusedIstreamPtr EmbedWidget(Widget &child_widget) noexcept;
    UnusedIstreamPtr OpenWidgetElement(Widget &child_widget) noexcept;
    void FoundWidget(Widget &child_widget) noexcept;
    bool CheckWidgetLookup(Widget &child_widget) noexcept;
    bool WidgetElementFinished(const XmlParserTag &tag,
                               Widget &child_widget) noexcept;

    Istream *StartCdataIstream() noexcept;
    void StopCdataIstream() noexcept;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class XmlParserHandler */
    bool OnXmlTagStart(const XmlParserTag &tag) noexcept override;
    bool OnXmlTagFinished(const XmlParserTag &tag) noexcept override;
    void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override;
    size_t OnXmlCdata(const char *p, size_t length, bool escaped,
                      off_t start) noexcept override;
    void OnXmlEof(off_t length) noexcept override;
    void OnXmlError(std::exception_ptr ep) noexcept override;

    /**
     * Is this a tag which can have a link attribute?
     */
    static constexpr bool IsLink(Tag tag) noexcept {
        return tag == Tag::A || tag == Tag::FORM ||
            tag == Tag::IMG || tag == Tag::SCRIPT ||
            tag == Tag::META || tag == Tag::META_REFRESH ||
            tag == Tag::META_URI_CONTENT ||
            tag == Tag::PARAM || tag == Tag::REWRITE_URI;
    }

    /**
     * Is this a HTML tag? (i.e. not a proprietary beng-proxy tag)
     */
    static constexpr bool IsHtml(Tag tag) noexcept {
        return tag == Tag::OTHER || (IsLink(tag) && tag != Tag::REWRITE_URI);
    }
};

bool
processable(const StringMap &headers)
{
    const char *content_type = headers.Get("content-type");
    return content_type != nullptr &&
        (strncmp(content_type, "text/html", 9) == 0 ||
         strncmp(content_type, "text/xml", 8) == 0 ||
         strncmp(content_type, "application/xml", 15) == 0 ||
         strncmp(content_type, "application/xhtml+xml", 21) == 0);
}


/*
 * async operation
 *
 */

void
XmlProcessor::Cancel() noexcept
{
    /* the request body was not yet submitted to the focused widget;
       dispose it now */
    container.for_focused.body.Clear();

    pool_unref(&container.pool);
    pool_unref(&caller_pool);

    Close();
}

/*
 * constructor
 *
 */

static XmlProcessor *
processor_new(struct pool &caller_pool,
              Widget &widget,
              struct processor_env &env,
              unsigned options) noexcept
{
    struct pool *pool = pool_new_linear(&caller_pool, "processor", 32768);

    return NewFromPool<XmlProcessor>(*pool, *pool, caller_pool, widget,
                                     env, options);
}

UnusedIstreamPtr
processor_process(struct pool &caller_pool, UnusedIstreamPtr input,
                  Widget &widget,
                  struct processor_env &env,
                  unsigned options)
{
    auto *processor = processor_new(caller_pool, widget, env, options);
    processor->lookup_id = nullptr;

    /* the text processor will expand entities */
    auto tee = istream_tee_new(processor->pool,
                               text_processor(processor->pool,
                                              std::move(input),
                                              widget, env),
                               *env.event_loop,
                               true, true);

    auto r = istream_replace_new(*env.event_loop, processor->pool,
                                 std::move(tee.first));

    processor->replace = std::move(r.second);
    processor->InitParser(std::move(tee.second));
    pool_unref(&processor->pool);

    if (processor->HasOptionRewriteUrl()) {
        processor->default_uri_rewrite.base = URI_BASE_TEMPLATE;
        processor->default_uri_rewrite.mode = RewriteUriMode::PARTIAL;
        processor->default_uri_rewrite.view[0] = 0;

        if (options & PROCESSOR_FOCUS_WIDGET) {
            processor->default_uri_rewrite.base = URI_BASE_WIDGET;
            processor->default_uri_rewrite.mode = RewriteUriMode::FOCUS;
        }
    }

    //XXX headers = processor_header_forward(pool, headers);
    return std::move(r.first);
}

void
processor_lookup_widget(struct pool &caller_pool,
                        UnusedIstreamPtr istream,
                        Widget &widget, const char *id,
                        struct processor_env &env,
                        unsigned options,
                        WidgetLookupHandler &handler,
                        CancellablePointer &cancel_ptr)
{
    assert(id != nullptr);

    if ((options & PROCESSOR_CONTAINER) == 0) {
        auto e = WidgetError(WidgetErrorCode::NOT_A_CONTAINER,
                             "Not a container");
        handler.WidgetLookupError(std::make_exception_ptr(e));
        return;
    }

    auto *processor = processor_new(caller_pool, widget, env, options);

    processor->lookup_id = id;

    processor->InitParser(std::move(istream));

    processor->handler = &handler;

    pool_ref(&caller_pool);

    cancel_ptr = *processor;
    processor->cancel_ptr = &cancel_ptr;

    auto &pool = processor->pool;

    do {
        processor->had_input = false;
    } while (parser_read(processor->parser) && processor->had_input);

    pool_unref(&pool);
}

void
XmlProcessor::PostponeUriRewrite(off_t start, off_t end,
                                 const void *value, size_t length) noexcept
{
    assert(start <= end);

    if (postponed_rewrite.pending)
        /* cannot rewrite more than one attribute per element */
        return;

    /* postpone the URI rewrite until the tag is finished: save the
       attribute value position, save the original attribute value and
       set the "pending" flag */

    postponed_rewrite.uri_start = start;
    postponed_rewrite.uri_end = end;

    bool success = postponed_rewrite.value.Set(value, length);

    for (unsigned i = 0; i < ARRAY_SIZE(postponed_rewrite.delete_); ++i)
        postponed_rewrite.delete_[i].start = 0;
    postponed_rewrite.pending = success;
}

void
XmlProcessor::DeleteUriRewrite(off_t start, off_t end) noexcept
{
    if (!postponed_rewrite.pending) {
        /* no URI attribute found yet: delete immediately */
        Replace(start, end, nullptr);
        return;
    }

    /* find a free position in the "delete" array */

    unsigned i = 0;
    while (postponed_rewrite.delete_[i].start > 0) {
        ++i;
        if (i >= ARRAY_SIZE(postponed_rewrite.delete_))
            /* no more room in the array */
            return;
    }

    /* postpone the delete until the URI attribute has been replaced */

    postponed_rewrite.delete_[i].start = start;
    postponed_rewrite.delete_[i].end = end;
}

inline void
XmlProcessor::PostponeRefreshRewrite(const XmlParserAttribute &attr) noexcept
{
    const auto end = attr.value.end();
    const char *p = attr.value.Find(';');
    if (p == nullptr || p + 7 > end || memcmp(p + 1, "URL='", 5) != 0 ||
        end[-1] != '\'')
        return;

    p += 6;

    /* postpone the URI rewrite until the tag is finished: save the
       attribute value position, save the original attribute value and
       set the "pending" flag */

    PostponeUriRewrite(attr.value_start + (p - attr.value.data),
                       attr.value_end - 1, p, end - 1 - p);
}

inline void
XmlProcessor::CommitUriRewrite() noexcept
{
    XmlParserAttribute uri_attribute;
    uri_attribute.value_start = postponed_rewrite.uri_start;
    uri_attribute.value_end = postponed_rewrite.uri_end;

    assert(postponed_rewrite.pending);

    postponed_rewrite.pending = false;

    /* rewrite the URI */

    uri_attribute.value = postponed_rewrite.value.ReadStringView();
    TransformUriAttribute(uri_attribute,
                          uri_rewrite.base,
                          uri_rewrite.mode,
                          uri_rewrite.view[0] != 0
                          ? uri_rewrite.view : nullptr);

    /* now delete all c:base/c:mode attributes which followed the
       URI */

    for (unsigned i = 0; i < ARRAY_SIZE(postponed_rewrite.delete_); ++i)
        if (postponed_rewrite.delete_[i].start > 0)
            Replace(postponed_rewrite.delete_[i].start,
                    postponed_rewrite.delete_[i].end,
                    nullptr);
}

/*
 * CDATA istream
 *
 */

void
XmlProcessor::StopCdataIstream() noexcept
{
    if (tag != Tag::STYLE_PROCESS)
        return;

    cdata_istream->DestroyEof();
    tag = Tag::STYLE;
}

void
XmlProcessor::CdataIstream::_Read() noexcept
{
    assert(processor.tag == Tag::STYLE_PROCESS);

    parser_read(processor.parser);
}

void
XmlProcessor::CdataIstream::_Close() noexcept
{
    assert(processor.tag == Tag::STYLE_PROCESS);

    processor.tag = Tag::STYLE;
    Destroy();
}

inline Istream *
XmlProcessor::StartCdataIstream() noexcept
{
    return cdata_istream = NewFromPool<CdataIstream>(pool, *this);
}

/*
 * parser callbacks
 *
 */

bool
XmlProcessor::OnProcessingInstruction(StringView name) noexcept
{
    if (!IsQuiet() && HasOptionRewriteUrl() &&
        name.Equals("cm4all-rewrite-uri")) {
        InitUriRewrite(Tag::REWRITE_URI);
        return true;
    }

    return false;
}

inline bool
XmlProcessor::OnStartElementInWidget(XmlParserTagType type,
                                     StringView name) noexcept
{
    if (type == XmlParserTagType::PI)
        return OnProcessingInstruction(name);

    if (name.StartsWith({"c:", 2}))
        name.skip_front(2);

    if (name.Equals("widget")) {
        if (type == XmlParserTagType::CLOSE)
            tag = Tag::WIDGET;
    } else if (name.Equals("path-info")) {
        tag = Tag::WIDGET_PATH_INFO;
    } else if (name.Equals("param") ||
               name.Equals("parameter")) {
        tag = Tag::WIDGET_PARAM;
        widget.param.name.Clear();
        widget.param.value.Clear();
    } else if (name.Equals("header")) {
        tag = Tag::WIDGET_HEADER;
        widget.param.name.Clear();
        widget.param.value.Clear();
    } else if (name.Equals("view")) {
        tag = Tag::WIDGET_VIEW;
    } else {
        tag = Tag::IGNORE;
        return false;
    }

    return true;
}

bool
XmlProcessor::OnXmlTagStart(const XmlParserTag &xml_tag) noexcept
{
    had_input = true;

    StopCdataIstream();

    if (tag == Tag::SCRIPT && !xml_tag.name.EqualsIgnoreCase("script"))
        /* workaround for bugged scripts: ignore all closing tags
           except </SCRIPT> */
        return false;

    tag = Tag::IGNORE;

    if (widget.widget != nullptr)
        return OnStartElementInWidget(xml_tag.type, xml_tag.name);

    if (xml_tag.type == XmlParserTagType::PI)
        return OnProcessingInstruction(xml_tag.name);

    if (xml_tag.name.Equals("c:widget")) {
        if ((options & PROCESSOR_CONTAINER) == 0 ||
            global_translate_cache == nullptr)
            return false;

        if (xml_tag.type == XmlParserTagType::CLOSE) {
            assert(widget.widget == nullptr);
            return false;
        }

        tag = Tag::WIDGET;
        widget.widget = NewFromPool<Widget>(widget.pool, widget.pool, nullptr);
        widget.params.Clear();

        widget.widget->parent = &container;

        return true;
    } else if (xml_tag.name.EqualsIgnoreCase("script")) {
        InitUriRewrite(Tag::SCRIPT);
        return true;
    } else if (!IsQuiet() && HasOptionStyle() &&
               xml_tag.name.EqualsIgnoreCase("style")) {
        tag = Tag::STYLE;
        return true;
    } else if (!IsQuiet() && HasOptionRewriteUrl()) {
        if (xml_tag.name.EqualsIgnoreCase("a")) {
            InitUriRewrite(Tag::A);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("link")) {
            /* this isn't actually an anchor, but we are only interested in
               the HREF attribute */
            InitUriRewrite(Tag::A);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("form")) {
            InitUriRewrite(Tag::FORM);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("img")) {
            InitUriRewrite(Tag::IMG);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("iframe") ||
                   xml_tag.name.EqualsIgnoreCase("embed") ||
                   xml_tag.name.EqualsIgnoreCase("video") ||
                   xml_tag.name.EqualsIgnoreCase("audio")) {
            /* this isn't actually an IMG, but we are only interested
               in the SRC attribute */
            InitUriRewrite(Tag::IMG);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("param")) {
            InitUriRewrite(Tag::PARAM);
            return true;
        } else if (xml_tag.name.EqualsIgnoreCase("meta")) {
            InitUriRewrite(Tag::META);
            return true;
        } else if (HasOptionPrefixAny()) {
            tag = Tag::OTHER;
            return true;
        } else {
            tag = Tag::IGNORE;
            return false;
        }
    } else if (HasOptionPrefixAny()) {
        tag = Tag::OTHER;
        return true;
    } else {
        tag = Tag::IGNORE;
        return false;
    }
}

static void
SplitString(StringView in, char separator,
            StringView &before, StringView &after) noexcept
{
    const char *x = in.Find(separator);

    if (x != nullptr) {
        before = {in.data, x};
        after = {x + 1, in.end()};
    } else {
        before = in;
        after = nullptr;
    }
}

inline void
XmlProcessor::TransformUriAttribute(const XmlParserAttribute &attr,
                                    enum uri_base base,
                                    RewriteUriMode mode,
                                    const char *view) noexcept
{
    StringView value = attr.value;
    if (value.StartsWith({"mailto:", 7}))
        /* ignore email links */
        return;

    if (uri_has_authority(value))
        /* can't rewrite if the specified URI is absolute */
        return;

    Widget *target_widget = nullptr;
    StringView child_id, suffix;

    switch (base) {
    case URI_BASE_TEMPLATE:
        /* no need to rewrite the attribute */
        return;

    case URI_BASE_WIDGET:
        target_widget = &container;
        break;

    case URI_BASE_CHILD:
        SplitString(value, '/', child_id, suffix);

        target_widget = container.FindChild(p_strdup(pool, child_id));
        if (target_widget == nullptr)
            return;

        value = suffix;
        break;

    case URI_BASE_PARENT:
        target_widget = container.parent;
        if (target_widget == nullptr)
            return;

        break;
    }

    assert(target_widget != nullptr);

    if (target_widget->cls == nullptr && target_widget->class_name == nullptr)
        return;

    const char *hash = value.Find('#');
    StringView fragment;
    if (hash != nullptr) {
        /* save the unescaped fragment part of the URI, don't pass it
           to rewrite_widget_uri() */
        fragment = {hash, value.end()};
        value = {value.data, hash};
    } else
        fragment = nullptr;

    auto istream =
        rewrite_widget_uri(pool, env,
                           *global_translate_cache,
                           *target_widget,
                           value, mode, target_widget == &container,
                           view,
                           &html_escape_class);
    if (!istream)
        return;

    if (!fragment.empty()) {
        /* escape and append the fragment to the new URI */
        auto s = istream_memory_new(pool,
                                    p_strdup(pool, fragment),
                                    fragment.size);
        s = istream_html_escape_new(pool, std::move(s));

        istream = istream_cat_new(pool, std::move(istream), std::move(s));
    }

    ReplaceAttributeValue(attr, std::move(istream));
}

static void
parser_widget_attr_finished(Widget *widget,
                            StringView name, StringView value) noexcept
{
    if (name.Equals("type")) {
        widget->SetClassName(value);
    } else if (name.Equals("id")) {
        if (!value.empty())
            widget->SetId(value);
    } else if (name.Equals("display")) {
        if (value.Equals("inline"))
            widget->display = Widget::Display::INLINE;
        else if (value.Equals("none"))
            widget->display = Widget::Display::NONE;
        else
            widget->display = Widget::Display::NONE;
    } else if (name.Equals("session")) {
        if (value.Equals("resource"))
            widget->session_scope = Widget::SessionScope::RESOURCE;
        else if (value.Equals("site"))
            widget->session_scope = Widget::SessionScope::SITE;
    }
}

gcc_pure
static enum uri_base
parse_uri_base(StringView s) noexcept
{
    if (s.Equals("widget"))
        return URI_BASE_WIDGET;
    else if (s.Equals("child"))
        return URI_BASE_CHILD;
    else if (s.Equals("parent"))
        return URI_BASE_PARENT;
    else
        return URI_BASE_TEMPLATE;
}

inline bool
XmlProcessor::LinkAttributeFinished(const XmlParserAttribute &attr) noexcept
{
    if (attr.name.Equals("c:base")) {
        uri_rewrite.base = parse_uri_base(attr.value);

        if (tag != Tag::REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    if (attr.name.Equals("c:mode")) {
        uri_rewrite.mode = parse_uri_mode(attr.value);

        if (tag != Tag::REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    if (attr.name.Equals("c:view") &&
        attr.value.size < sizeof(uri_rewrite.view)) {
        memcpy(uri_rewrite.view,
               attr.value.data, attr.value.size);
        uri_rewrite.view[attr.value.size] = 0;

        if (tag != Tag::REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);

        return true;
    }

    if (attr.name.Equals("xmlns:c")) {
        /* delete "xmlns:c" attributes */
        if (tag != Tag::REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    return false;
}

static const char *
find_underscore(const char *p, const char *end) noexcept
{
    assert(p != nullptr);
    assert(end != nullptr);
    assert(p <= end);

    if (p == end)
        return nullptr;

    if (is_underscore_prefix(p, end))
        return p;

    while (true) {
        p = (const char *)memchr(p + 1, '_', end - p);
        if (p == nullptr)
            return nullptr;

        if (IsWhitespaceOrNull(p[-1]) &&
            is_underscore_prefix(p, end))
            return p;
    }
}

inline void
XmlProcessor::HandleClassAttribute(const XmlParserAttribute &attr) noexcept
{
    auto p = attr.value.begin();
    const auto end = attr.value.end();

    const char *u = find_underscore(p, end);
    if (u == nullptr)
        return;

    buffer.Clear();

    do {
        if (!buffer.Write(p, u - p))
            return;

        p = u;

        const unsigned n = underscore_prefix(p, end);
        const char *prefix;
        if (n == 3 && (prefix = container.GetPrefix()) != nullptr) {
            if (!buffer.Write(prefix))
                return;

            p += 3;
        } else if (n == 2 && (prefix = container.GetQuotedClassName()) != nullptr) {
            if (!buffer.Write(prefix))
                return;

            p += 2;
        } else {
            /* failure; skip all underscores and find the next
               match */
            while (u < end && *u == '_')
                ++u;

            if (!buffer.Write(p, u - p))
                return;

            p = u;
        }

        u = find_underscore(p, end);
    } while (u != nullptr);

    if (!buffer.Write(p, end - p))
        return;

    const size_t length = buffer.GetSize();
    void *q = buffer.Dup(pool);
    ReplaceAttributeValue(attr, istream_memory_new(pool, q, length));
}

void
XmlProcessor::HandleIdAttribute(const XmlParserAttribute &attr) noexcept
{
    auto p = attr.value.begin();
    const auto end = attr.value.end();

    const unsigned n = underscore_prefix(p, end);
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = container.GetPrefix();
        if (prefix == nullptr)
            return;

        Replace(attr.value_start, attr.value_start + 3,
                istream_string_new(pool, prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name = container.GetQuotedClassName();
        if (class_name == nullptr)
            return;

        Replace(attr.value_start, attr.value_start + 2,
                istream_string_new(pool, class_name));
    }
}

void
XmlProcessor::HandleStyleAttribute(const XmlParserAttribute &attr) noexcept
{
    auto result =
        css_rewrite_block_uris(pool, env,
                               *global_translate_cache,
                               container,
                               attr.value,
                               &html_escape_class);
    if (result)
        ReplaceAttributeValue(attr, std::move(result));
}

gcc_pure
static bool
IsMetaPropertyWithLink(StringView property) noexcept
{
    return property.StartsWith("og:") &&
        (property.EndsWith(":url") ||
         property.Equals("og:image") ||
         property.Equals("og:audio") ||
         property.Equals("og:video"));
}

/**
 * Does this attribute indicate that the "meta" element contains an
 * URI in the "content" attribute?
 */
gcc_pure
static bool
IsMetaWithUriContent(StringView name, StringView value) noexcept
{
    return name.EqualsIgnoreCase("property") && IsMetaPropertyWithLink(value);
}

void
XmlProcessor::OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept
{
    had_input = true;

    if (!IsQuiet()) {
        if (IsLink(tag) &&
            LinkAttributeFinished(attr))
            return;

        if (tag == Tag::META &&
            attr.name.EqualsIgnoreCase("http-equiv") &&
            attr.value.EqualsIgnoreCase("refresh")) {
            /* morph Tag::META to Tag::META_REFRESH */
            tag = Tag::META_REFRESH;
            return;
        }

        if (tag == Tag::META && IsMetaWithUriContent(attr.name, attr.value)) {
            /* morph Tag::META to Tag::META_URI_CONTENT */
            tag = Tag::META_URI_CONTENT;
            return;
        }

        if (HasOptionPrefixClass() &&
            /* due to a limitation in the processor and istream_replace,
               we cannot edit attributes followed by a URI attribute */
            !postponed_rewrite.pending &&
            IsHtml(tag) &&
            attr.name.Equals("class")) {
            HandleClassAttribute(attr);
            return;
        }

        if (HasOptionPrefixId() &&
            /* due to a limitation in the processor and istream_replace,
               we cannot edit attributes followed by a URI attribute */
            !postponed_rewrite.pending &&
            IsHtml(tag) &&
            (attr.name.Equals("id") || attr.name.Equals("for"))) {
            HandleIdAttribute(attr);
            return;
        }

        if (HasOptionStyle() && HasOptionRewriteUrl() &&
            /* due to a limitation in the processor and istream_replace,
               we cannot edit attributes followed by a URI attribute */
            !postponed_rewrite.pending &&
            IsHtml(tag) &&
            attr.name.Equals("style")) {
            HandleStyleAttribute(attr);
            return;
        }
    }

    switch (tag) {
    case Tag::NONE:
    case Tag::IGNORE:
    case Tag::OTHER:
        break;

    case Tag::WIDGET:
        assert(widget.widget != nullptr);

        parser_widget_attr_finished(widget.widget,
                                    attr.name, attr.value);
        break;

    case Tag::WIDGET_PARAM:
    case Tag::WIDGET_HEADER:
        assert(widget.widget != nullptr);

        if (attr.name.Equals("name")) {
            widget.param.name.Set(attr.value);
        } else if (attr.name.Equals("value")) {
            widget.param.value.Set(attr.value);
        }

        break;

    case Tag::WIDGET_PATH_INFO:
        assert(widget.widget != nullptr);

        if (attr.name.Equals("value"))
            widget.widget->from_template.path_info
                = p_strdup(widget.pool, attr.value);

        break;

    case Tag::WIDGET_VIEW:
        assert(widget.widget != nullptr);

        if (attr.name.Equals("name")) {
            if (attr.value.empty()) {
                container.logger(2, "empty view name");
                return;
            }

            widget.widget->from_template.view_name =
                p_strdup(widget.pool, attr.value);
        }

        break;

    case Tag::IMG:
        if (attr.name.EqualsIgnoreCase("src"))
            PostponeUriRewrite(attr);
        break;

    case Tag::A:
        if (attr.name.EqualsIgnoreCase("href")) {
            if (!attr.value.StartsWith({"#", 1}) &&
                !attr.value.StartsWith({"javascript:", 11}))
                PostponeUriRewrite(attr);
        } else if (IsQuiet() &&
                   HasOptionPrefixId() &&
                   attr.name.EqualsIgnoreCase("name"))
            HandleIdAttribute(attr);

        break;

    case Tag::FORM:
        if (attr.name.EqualsIgnoreCase("action"))
            PostponeUriRewrite(attr);
        break;

    case Tag::SCRIPT:
        if (!IsQuiet() &&
            HasOptionRewriteUrl() &&
            attr.name.EqualsIgnoreCase("src"))
            PostponeUriRewrite(attr);
        break;

    case Tag::PARAM:
        if (attr.name.Equals("value"))
            PostponeUriRewrite(attr);
        break;

    case Tag::META_REFRESH:
        if (attr.name.EqualsIgnoreCase("content"))
            PostponeRefreshRewrite(attr);
        break;

    case Tag::META_URI_CONTENT:
        if (attr.name.EqualsIgnoreCase("content"))
            PostponeUriRewrite(attr);
        break;

    case Tag::REWRITE_URI:
    case Tag::STYLE:
    case Tag::STYLE_PROCESS:
    case Tag::META:
        break;
    }
}

static std::exception_ptr
widget_catch_callback(std::exception_ptr ep, void *ctx) noexcept
{
    auto *widget = (Widget *)ctx;

    widget->logger(3, ep);
    return {};
}

inline void
XmlProcessor::PrepareEmbedWidget(Widget &child_widget)
{
    if (child_widget.class_name == nullptr)
        throw std::runtime_error("widget without a class");

    /* enforce the SELF_CONTAINER flag */
    const bool self_container =
        (options & PROCESSOR_SELF_CONTAINER) != 0;
    if (!widget_init_approval(&child_widget, self_container))
        throw FormatRuntimeError("widget is not allowed to embed widget '%s'",
                                 child_widget.GetLogName());

    if (widget_check_recursion(child_widget.parent))
        throw FormatRuntimeError("maximum widget depth exceeded for widget '%s'",
                                 child_widget.GetLogName());

    if (!widget.params.IsEmpty())
        child_widget.from_template.query_string =
            widget.params.StringDup(widget.pool);

    container.children.push_front(child_widget);
}

inline UnusedIstreamPtr
XmlProcessor::EmbedWidget(Widget &child_widget) noexcept
{
    assert(child_widget.class_name != nullptr);

    try {
        child_widget.CopyFromRequest();
    } catch (...) {
        child_widget.Cancel();
        return nullptr;
    }

    if (child_widget.display == Widget::Display::NONE) {
        child_widget.Cancel();
        return nullptr;
    }

    auto istream = embed_inline_widget(pool, env, false,
                                       child_widget);
    if (istream)
        istream = istream_catch_new(&pool, std::move(istream),
                                    widget_catch_callback, &child_widget);

    return istream;
}

inline UnusedIstreamPtr
XmlProcessor::OpenWidgetElement(Widget &child_widget) noexcept
{
    assert(child_widget.parent == &container);

    try {
        PrepareEmbedWidget(child_widget);
    } catch (...) {
        container.logger(5, std::current_exception());
        return nullptr;
    }

    return EmbedWidget(child_widget);
}

inline void
XmlProcessor::FoundWidget(Widget &child_widget) noexcept
{
    assert(child_widget.parent == &container);
    assert(!replace);

    auto &_caller_pool = caller_pool, &widget_pool = container.pool;
    auto &handler2 = *handler;

    try {
        {
            AtScopeExit(this) { Close(); };
            PrepareEmbedWidget(child_widget);
        }

        child_widget.CopyFromRequest();
        handler2.WidgetFound(child_widget);
    } catch (...) {
        child_widget.Cancel();
        handler2.WidgetLookupError(std::current_exception());
    }

    pool_unref(&_caller_pool);
    pool_unref(&widget_pool);
}

inline bool
XmlProcessor::CheckWidgetLookup(Widget &child_widget) noexcept
{
    assert(child_widget.parent == &container);
    assert(!replace);

    if (child_widget.id != nullptr && strcmp(lookup_id, child_widget.id) == 0) {
        FoundWidget(child_widget);
        return false;
    } else {
        child_widget.Cancel();
        return true;
    }
}

inline bool
XmlProcessor::WidgetElementFinished(const XmlParserTag &widget_tag,
                                    Widget &child_widget) noexcept
{
    if (replace) {
        Replace(widget.start_offset, widget_tag.end, OpenWidgetElement(child_widget));
        return true;
    } else
        return CheckWidgetLookup(child_widget);
}

gcc_pure
static bool
header_name_valid(const char *name, size_t length) noexcept
{
    /* name must start with "X-" */
    if (length < 3 ||
        (name[0] != 'x' && name[0] != 'X') ||
        name[1] != '-')
        return false;

    /* the rest must be letters, digits or dash */
    for (size_t i = 2; i < length;  ++i)
        if (!IsAlphaNumericASCII(name[i]) && name[i] != '-')
            return false;

    return true;
}

static void
expansible_buffer_append_uri_escaped(ExpansibleBuffer &buffer,
                                     StringView value) noexcept
{
    char *escaped = (char *)p_malloc(tpool, value.size * 3);
    size_t length = uri_escape(escaped, StringView(value.data, value.size));
    buffer.Write(escaped, length);
}

bool
XmlProcessor::OnXmlTagFinished(const XmlParserTag &xml_tag) noexcept
{
    had_input = true;

    if (postponed_rewrite.pending)
        CommitUriRewrite();

    if (tag == Tag::WIDGET) {
        if (xml_tag.type == XmlParserTagType::OPEN || xml_tag.type == XmlParserTagType::SHORT)
            widget.start_offset = xml_tag.start;
        else if (widget.widget == nullptr)
            return true;

        assert(widget.widget != nullptr);

        if (xml_tag.type == XmlParserTagType::OPEN)
            return true;

        auto &child_widget = *widget.widget;
        widget.widget = nullptr;

        return WidgetElementFinished(xml_tag, child_widget);
    } else if (tag == Tag::WIDGET_PARAM) {
        assert(widget.widget != nullptr);

        if (widget.param.name.IsEmpty())
            return true;

        const AutoRewindPool auto_rewind(*tpool);

        auto value = widget.param.value.ReadStringView();
        if (value.Find('&') != nullptr) {
            char *q = (char *)p_memdup(tpool, value.data, value.size);
            value.size = unescape_inplace(&html_escape_class, q, value.size);
            value.data = q;
        }

        if (!widget.params.IsEmpty())
            widget.params.Write("&", 1);

        const auto name = widget.param.name.ReadStringView();
        expansible_buffer_append_uri_escaped(widget.params, name);

        widget.params.Write("=", 1);

        expansible_buffer_append_uri_escaped(widget.params, value);
    } else if (tag == Tag::WIDGET_HEADER) {
        assert(widget.widget != nullptr);

        if (xml_tag.type == XmlParserTagType::CLOSE)
            return true;

        const auto name = widget.param.name.ReadStringView();
        if (!header_name_valid(name.data, name.size)) {
            container.logger(3, "invalid widget HTTP header name");
            return true;
        }

        if (widget.widget->from_template.headers == nullptr)
            widget.widget->from_template.headers = strmap_new(&widget.pool);

        char *value = widget.param.value.StringDup(widget.pool);
        if (strchr(value, '&') != nullptr) {
            size_t length = unescape_inplace(&html_escape_class,
                                             value, strlen(value));
            value[length] = 0;
        }

        widget.widget->from_template.headers->Add(widget.param.name.StringDup(widget.pool),
                                                  value);
    } else if (tag == Tag::SCRIPT) {
        if (xml_tag.type == XmlParserTagType::OPEN)
            parser_script(parser);
        else
            tag = Tag::NONE;
    } else if (tag == Tag::REWRITE_URI) {
        /* the settings of this tag become the new default */
        default_uri_rewrite = uri_rewrite;

        Replace(xml_tag.start, xml_tag.end, nullptr);
    } else if (tag == Tag::STYLE) {
        if (xml_tag.type == XmlParserTagType::OPEN && !IsQuiet() && HasOptionStyle()) {
            /* create a CSS processor for the contents of this style
               element */

            tag = Tag::STYLE_PROCESS;

            unsigned css_options = 0;
            if (options & PROCESSOR_REWRITE_URL)
                css_options |= CSS_PROCESSOR_REWRITE_URL;
            if (options & PROCESSOR_PREFIX_CSS_CLASS)
                css_options |= CSS_PROCESSOR_PREFIX_CLASS;
            if (options & PROCESSOR_PREFIX_XML_ID)
                css_options |= CSS_PROCESSOR_PREFIX_ID;

            auto istream =
                css_processor(pool, UnusedIstreamPtr(StartCdataIstream()),
                              container, env,
                              css_options);

            /* the end offset will be extended later with
               istream_replace_extend() */
            cdata_start = xml_tag.end;
            Replace(xml_tag.end, xml_tag.end, std::move(istream));
        }
    }

    return true;
}

size_t
XmlProcessor::OnXmlCdata(const char *p gcc_unused, size_t length,
                         gcc_unused bool escaped, off_t start) noexcept
{
    had_input = true;

    if (tag == Tag::STYLE_PROCESS) {
        /* XXX unescape? */
        length = cdata_istream->InvokeData(p, length);
        if (length > 0)
            replace->Extend(cdata_start, start + length);
    } else if (replace && widget.widget == nullptr)
        replace->Settle(start + length);

    return length;
}

void
XmlProcessor::OnXmlEof(gcc_unused off_t length) noexcept
{
    auto &widget_pool = container.pool;

    assert(parser != nullptr);

    StopCdataIstream();

    /* the request body could not be submitted to the focused widget,
       because we didn't find it; dispose it now */
    container.for_focused.body.Clear();

    if (replace)
        replace->Finish();

    if (lookup_id != nullptr) {
        /* widget was not found */

        handler->WidgetNotFound();
        pool_unref(&caller_pool);
    }

    pool_unref(&widget_pool);

    Destroy();
}

void
XmlProcessor::OnXmlError(std::exception_ptr ep) noexcept
{
    auto &widget_pool = container.pool;

    assert(parser != nullptr);

    StopCdataIstream();

    /* the request body could not be submitted to the focused widget,
       because we didn't find it; dispose it now */
    container.for_focused.body.Clear();

    if (lookup_id != nullptr) {
        handler->WidgetLookupError(ep);
        pool_unref(&caller_pool);
    }

    pool_unref(&widget_pool);

    Destroy();
}
