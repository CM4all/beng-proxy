/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.hxx"
#include "text_processor.hxx"
#include "css_processor.hxx"
#include "css_rewrite.hxx"
#include "penv.hxx"
#include "xml_parser.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "widget.hxx"
#include "widget_approval.hxx"
#include "widget_request.hxx"
#include "widget_lookup.hxx"
#include "widget_class.hxx"
#include "widget-quark.h"
#include "growing_buffer.hxx"
#include "tpool.hxx"
#include "inline_widget.hxx"
#include "async.hxx"
#include "rewrite_uri.hxx"
#include "bp_global.hxx"
#include "expansible_buffer.hxx"
#include "escape_class.hxx"
#include "escape_html.hxx"
#include "strmap.hxx"
#include "css_syntax.hxx"
#include "css_util.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream.hxx"
#include "istream/istream.hxx"
#include "istream/istream_replace.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_catch.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_tee.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"
#include "util/Macros.hxx"
#include "util/StringView.hxx"

#include <daemon/log.h>

#include <glib.h>

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
    enum uri_mode mode;

    char view[64];
};

enum tag {
    TAG_NONE,
    TAG_IGNORE,
    TAG_OTHER,
    TAG_WIDGET,
    TAG_WIDGET_PATH_INFO,
    TAG_WIDGET_PARAM,
    TAG_WIDGET_HEADER,
    TAG_WIDGET_VIEW,
    TAG_A,
    TAG_FORM,
    TAG_IMG,
    TAG_SCRIPT,
    TAG_PARAM,
    TAG_REWRITE_URI,

    /**
     * The "meta" element.  This may morph into #TAG_META_REFRESH when
     * an http-equiv="refresh" attribute is found.
     */
    TAG_META,

    TAG_META_REFRESH,

    /**
     * The "style" element.  This value later morphs into
     * #TAG_STYLE_PROCESS if #PROCESSOR_STYLE is enabled.
     */
    TAG_STYLE,

    /**
     * Only used when #PROCESSOR_STYLE is enabled.  If active, then
     * CDATA is being fed into the CSS processor.
     */
    TAG_STYLE_PROCESS,
};

struct XmlProcessor final : XmlParserHandler {
    class CdataIstream final : public Istream {
        friend struct XmlProcessor;
        XmlProcessor &processor;

    public:
        explicit CdataIstream(XmlProcessor &_processor)
            :Istream(*_processor.pool), processor(_processor) {}

        /* virtual methods from class Istream */
        void _Read() override;
        void _Close() override;
    };

    struct pool *const pool, *const caller_pool;

    Widget *const container;
    const char *lookup_id;
    struct processor_env *const env;
    const unsigned options;

    Istream *replace;

    XmlParser *parser;
    bool had_input;

    enum tag tag = TAG_NONE;

    struct uri_rewrite uri_rewrite;

    /**
     * The default value for #uri_rewrite.
     */
    struct uri_rewrite default_uri_rewrite;

    /**
     * A buffer that may be used for various temporary purposes
     * (e.g. attribute transformation).
     */
    struct expansible_buffer *buffer;

    /**
     * These values are used to buffer c:mode/c:base values in any
     * order, even after the actual URI attribute.
     */
    struct PostponedRewrite {
        bool pending = false;

        off_t uri_start, uri_end;
        struct expansible_buffer *const value;

        /**
         * The positions of the c:mode/c:base attributes after the URI
         * attribute.  These have to be deleted *after* the URI
         * attribute has been rewritten.
         */
        struct {
            off_t start, end;
        } delete_[4];

        PostponedRewrite(struct pool &pool)
            :value(expansible_buffer_new(&pool, 1024, 8192)) {}
    } postponed_rewrite;

    struct CurrentWidget {
        off_t start_offset;

        struct pool *const pool;
        Widget *widget = nullptr;

        struct Param {
            struct expansible_buffer *const name;
            struct expansible_buffer *const value;

            Param(struct pool &pool)
                :name(expansible_buffer_new(&pool, 128, 512)),
                 value(expansible_buffer_new(&pool, 512, 4096)) {}
        } param;

        struct expansible_buffer *params;

        CurrentWidget(struct pool &processor_pool, struct processor_env &env)
            :pool(env.pool), param(processor_pool),
             params(expansible_buffer_new(&processor_pool, 1024, 8192)) {}
    } widget;

    /**
     * Only valid if #cdata_stream_active is true.
     */
    off_t cdata_start;
    CdataIstream *cdata_istream;

    struct async_operation async;

    WidgetLookupHandler *handler;

    struct async_operation_ref *async_ref;

    XmlProcessor(struct pool &_pool, struct pool &_caller_pool,
                 Widget &_widget, struct processor_env &_env,
                 unsigned _options)
        :pool(&_pool), caller_pool(&_caller_pool),
         container(&_widget),
         env(&_env), options(_options),
         buffer(expansible_buffer_new(pool, 128, 2048)),
         postponed_rewrite(*pool),
         widget(*pool, *env) {
        pool_ref(container->pool);
    }

    bool IsQuiet() const {
        return replace == nullptr;
    }

    bool HasOptionRewriteUrl() const {
        return (options & PROCESSOR_REWRITE_URL) != 0;
    }

    bool HasOptionPrefixClass() const {
        return (options & PROCESSOR_PREFIX_CSS_CLASS) != 0;
    }

    bool HasOptionPrefixId() const {
        return (options & PROCESSOR_PREFIX_XML_ID) != 0;
    }

    bool HasOptionPrefixAny() const {
        return (options & (PROCESSOR_PREFIX_CSS_CLASS|PROCESSOR_PREFIX_XML_ID)) != 0;
    }

    bool HasOptionStyle() const {
        return (options & PROCESSOR_STYLE) != 0;
    }

    void Replace(off_t start, off_t end, Istream *istream) {
        istream_replace_add(*replace, start, end, istream);
    }

    void ReplaceAttributeValue(const XmlParserAttribute &attr,
                               Istream *value) {
        Replace(attr.value_start, attr.value_end, value);
    }

    void InitUriRewrite(enum tag _tag) {
        assert(!postponed_rewrite.pending);

        tag = _tag;
        uri_rewrite = default_uri_rewrite;
    }

    void PostponeUriRewrite(off_t start, off_t end,
                            const void *value, size_t length);

    void PostponeUriRewrite(const XmlParserAttribute &attr) {
        PostponeUriRewrite(attr.value_start, attr.value_end,
                           attr.value.data, attr.value.size);
    }

    void PostponeRefreshRewrite(const XmlParserAttribute &attr);

    void CommitUriRewrite();

    void DeleteUriRewrite(off_t start, off_t end);

    void TransformUriAttribute(const XmlParserAttribute &attr,
                               enum uri_base base, enum uri_mode mode,
                               const char *view);

    bool LinkAttributeFinished(const XmlParserAttribute &attr);
    void HandleClassAttribute(const XmlParserAttribute &attr);
    void HandleIdAttribute(const XmlParserAttribute &attr);
    void HandleStyleAttribute(const XmlParserAttribute &attr);

    Istream *EmbedWidget(Widget &child_widget);
    Istream *OpenWidgetElement(Widget &child_widget);
    void WidgetElementFinished(const XmlParserTag &tag,
                               Widget &child_widget);

    Istream *StartCdataIstream();
    void StopCdataIstream();

    void Abort();

    /* virtual methods from class XmlParserHandler */
    bool OnXmlTagStart(const XmlParserTag &tag) override;
    void OnXmlTagFinished(const XmlParserTag &tag) override;
    void OnXmlAttributeFinished(const XmlParserAttribute &attr) override;
    size_t OnXmlCdata(const char *p, size_t length, bool escaped,
                      off_t start) override;
    void OnXmlEof(off_t length) override;
    void OnXmlError(GError *error) override;
};

bool
processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
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

inline void
XmlProcessor::Abort()
{
    if (container->for_focused.body != nullptr)
        /* the request body was not yet submitted to the focused
           widget; dispose it now */
        istream_free_unused(&container->for_focused.body);

    pool_unref(container->pool);
    pool_unref(caller_pool);

    if (parser != nullptr)
        parser_close(parser);
}

/*
 * constructor
 *
 */

static void
processor_parser_init(XmlProcessor &processor, Istream &input);

static XmlProcessor *
processor_new(struct pool &caller_pool,
              Widget &widget,
              struct processor_env &env,
              unsigned options)
{
    struct pool *pool = pool_new_linear(&caller_pool, "processor", 32768);

    return NewFromPool<XmlProcessor>(*pool, *pool, caller_pool, widget,
                                     env, options);
}

Istream *
processor_process(struct pool &caller_pool, Istream &input,
                  Widget &widget,
                  struct processor_env &env,
                  unsigned options)
{
    auto *processor = processor_new(caller_pool, widget, env, options);
    processor->lookup_id = nullptr;

    /* the text processor will expand entities */
    auto *istream = text_processor(*processor->pool, input, widget, env);

    Istream *tee = istream_tee_new(*processor->pool, *istream,
                                   *env.event_loop,
                                   true, true);
    istream = &istream_tee_second(*tee);
    processor->replace = istream_replace_new(*processor->pool, *tee);

    processor_parser_init(*processor, *istream);
    pool_unref(processor->pool);

    if (processor->HasOptionRewriteUrl()) {
        processor->default_uri_rewrite.base = URI_BASE_TEMPLATE;
        processor->default_uri_rewrite.mode = URI_MODE_PARTIAL;
        processor->default_uri_rewrite.view[0] = 0;

        if (options & PROCESSOR_FOCUS_WIDGET) {
            processor->default_uri_rewrite.base = URI_BASE_WIDGET;
            processor->default_uri_rewrite.mode = URI_MODE_FOCUS;
        }
    }

    //XXX headers = processor_header_forward(pool, headers);
    return processor->replace;
}

void
processor_lookup_widget(struct pool &caller_pool,
                        Istream &istream,
                        Widget &widget, const char *id,
                        struct processor_env &env,
                        unsigned options,
                        WidgetLookupHandler &handler,
                        struct async_operation_ref &async_ref)
{
    assert(id != nullptr);

    if ((options & PROCESSOR_CONTAINER) == 0) {
        GError *error =
            g_error_new_literal(widget_quark(), WIDGET_ERROR_NOT_A_CONTAINER,
                                "Not a container");
        handler.WidgetLookupError(error);
        return;
    }

    auto *processor = processor_new(caller_pool, widget, env, options);

    processor->lookup_id = id;

    processor->replace = nullptr;

    processor_parser_init(*processor, istream);

    processor->handler = &handler;

    pool_ref(&caller_pool);

    processor->async.Init2<XmlProcessor, &XmlProcessor::async>();
    async_ref.Set(processor->async);
    processor->async_ref = &async_ref;

    do {
        processor->had_input = false;
        parser_read(processor->parser);
    } while (processor->had_input && processor->parser != nullptr);

    pool_unref(processor->pool);
}

void
XmlProcessor::PostponeUriRewrite(off_t start, off_t end,
                                 const void *value, size_t length)
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

    bool success = expansible_buffer_set(postponed_rewrite.value,
                                         value, length);

    for (unsigned i = 0; i < ARRAY_SIZE(postponed_rewrite.delete_); ++i)
        postponed_rewrite.delete_[i].start = 0;
    postponed_rewrite.pending = success;
}

void
XmlProcessor::DeleteUriRewrite(off_t start, off_t end)
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
XmlProcessor::PostponeRefreshRewrite(const XmlParserAttribute &attr)
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
XmlProcessor::CommitUriRewrite()
{
    XmlParserAttribute uri_attribute;
    uri_attribute.value_start = postponed_rewrite.uri_start;
    uri_attribute.value_end = postponed_rewrite.uri_end;

    assert(postponed_rewrite.pending);

    postponed_rewrite.pending = false;

    /* rewrite the URI */

    uri_attribute.value = expansible_buffer_read_string_view(postponed_rewrite.value);
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
XmlProcessor::StopCdataIstream()
{
    if (tag != TAG_STYLE_PROCESS)
        return;

    cdata_istream->DestroyEof();
    tag = TAG_STYLE;
}

void
XmlProcessor::CdataIstream::_Read()
{
    assert(processor.tag == TAG_STYLE_PROCESS);

    parser_read(processor.parser);
}

void
XmlProcessor::CdataIstream::_Close()
{
    assert(processor.tag == TAG_STYLE_PROCESS);

    processor.tag = TAG_STYLE;
    Destroy();
}

inline Istream *
XmlProcessor::StartCdataIstream()
{
    return cdata_istream = NewFromPool<CdataIstream>(*pool, *this);
}

/*
 * parser callbacks
 *
 */

static bool
processor_processing_instruction(XmlProcessor *processor,
                                 StringView name)
{
    if (!processor->IsQuiet() &&
        processor->HasOptionRewriteUrl() &&
        name.EqualsLiteral("cm4all-rewrite-uri")) {
        processor->InitUriRewrite(TAG_REWRITE_URI);
        return true;
    }

    return false;
}

static bool
parser_element_start_in_widget(XmlProcessor *processor,
                               XmlParserTagType type,
                               StringView name)
{
    if (type == XmlParserTagType::PI)
        return processor_processing_instruction(processor, name);

    if (name.StartsWith({"c:", 2}))
        name.skip_front(2);

    if (name.EqualsLiteral("widget")) {
        if (type == XmlParserTagType::CLOSE)
            processor->tag = TAG_WIDGET;
    } else if (name.EqualsLiteral("path-info")) {
        processor->tag = TAG_WIDGET_PATH_INFO;
    } else if (name.EqualsLiteral("param") ||
               name.EqualsLiteral("parameter")) {
        processor->tag = TAG_WIDGET_PARAM;
        expansible_buffer_reset(processor->widget.param.name);
        expansible_buffer_reset(processor->widget.param.value);
    } else if (name.EqualsLiteral("header")) {
        processor->tag = TAG_WIDGET_HEADER;
        expansible_buffer_reset(processor->widget.param.name);
        expansible_buffer_reset(processor->widget.param.value);
    } else if (name.EqualsLiteral("view")) {
        processor->tag = TAG_WIDGET_VIEW;
    } else {
        processor->tag = TAG_IGNORE;
        return false;
    }

    return true;
}

bool
XmlProcessor::OnXmlTagStart(const XmlParserTag &xml_tag)
{
    had_input = true;

    StopCdataIstream();

    if (tag == TAG_SCRIPT && !xml_tag.name.EqualsLiteralIgnoreCase("script"))
        /* workaround for bugged scripts: ignore all closing tags
           except </SCRIPT> */
        return false;

    tag = TAG_IGNORE;

    if (widget.widget != nullptr)
        return parser_element_start_in_widget(this, xml_tag.type,
                                              xml_tag.name);

    if (xml_tag.type == XmlParserTagType::PI)
        return processor_processing_instruction(this, xml_tag.name);

    if (xml_tag.name.EqualsLiteral("c:widget")) {
        if ((options & PROCESSOR_CONTAINER) == 0 ||
            global_translate_cache == nullptr)
            return false;

        if (xml_tag.type == XmlParserTagType::CLOSE) {
            assert(widget.widget == nullptr);
            return false;
        }

        tag = TAG_WIDGET;
        widget.widget = NewFromPool<Widget>(*widget.pool);
        widget.widget->Init(*widget.pool, nullptr);
        expansible_buffer_reset(widget.params);

        widget.widget->parent = container;

        return true;
    } else if (xml_tag.name.EqualsLiteralIgnoreCase("script")) {
        InitUriRewrite(TAG_SCRIPT);
        return true;
    } else if (!IsQuiet() && HasOptionStyle() &&
               xml_tag.name.EqualsLiteralIgnoreCase("style")) {
        tag = TAG_STYLE;
        return true;
    } else if (!IsQuiet() && HasOptionRewriteUrl()) {
        if (xml_tag.name.EqualsLiteralIgnoreCase("a")) {
            InitUriRewrite(TAG_A);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("link")) {
            /* this isn't actually an anchor, but we are only interested in
               the HREF attribute */
            InitUriRewrite(TAG_A);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("form")) {
            InitUriRewrite(TAG_FORM);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("img")) {
            InitUriRewrite(TAG_IMG);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("iframe") ||
                   xml_tag.name.EqualsLiteralIgnoreCase("embed") ||
                   xml_tag.name.EqualsLiteralIgnoreCase("video") ||
                   xml_tag.name.EqualsLiteralIgnoreCase("audio")) {
            /* this isn't actually an IMG, but we are only interested
               in the SRC attribute */
            InitUriRewrite(TAG_IMG);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("param")) {
            InitUriRewrite(TAG_PARAM);
            return true;
        } else if (xml_tag.name.EqualsLiteralIgnoreCase("meta")) {
            InitUriRewrite(TAG_META);
            return true;
        } else if (HasOptionPrefixAny()) {
            tag = TAG_OTHER;
            return true;
        } else {
            tag = TAG_IGNORE;
            return false;
        }
    } else if (HasOptionPrefixAny()) {
        tag = TAG_OTHER;
        return true;
    } else {
        tag = TAG_IGNORE;
        return false;
    }
}

static void
SplitString(StringView in, char separator,
            StringView &before, StringView &after)
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
                                    enum uri_mode mode,
                                    const char *view)
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
        target_widget = container;
        break;

    case URI_BASE_CHILD:
        SplitString(value, '/', child_id, suffix);

        target_widget = container->FindChild(p_strdup(*pool, child_id));
        if (target_widget == nullptr)
            return;

        value = suffix;
        break;

    case URI_BASE_PARENT:
        target_widget = container->parent;
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

    Istream *istream =
        rewrite_widget_uri(*pool,
                           *env,
                           *global_translate_cache,
                           *target_widget,
                           value, mode, target_widget == container,
                           view,
                           &html_escape_class);
    if (istream == nullptr)
        return;

    if (!fragment.IsEmpty()) {
        /* escape and append the fragment to the new URI */
        Istream *s = istream_memory_new(pool,
                                        p_strdup(*pool, fragment),
                                        fragment.size);
        s = istream_html_escape_new(*pool, *s);

        istream = istream_cat_new(*pool, istream, s);
    }

    ReplaceAttributeValue(attr, istream);
}

static void
parser_widget_attr_finished(Widget *widget,
                            StringView name, StringView value)
{
    if (name.EqualsLiteral("type")) {
        widget->SetClassName(value);
    } else if (name.EqualsLiteral("id")) {
        if (!value.IsEmpty())
            widget->SetId(value);
    } else if (name.EqualsLiteral("display")) {
        if (value.EqualsLiteral("inline"))
            widget->display = Widget::WIDGET_DISPLAY_INLINE;
        else if (value.EqualsLiteral("none"))
            widget->display = Widget::WIDGET_DISPLAY_NONE;
        else
            widget->display = Widget::WIDGET_DISPLAY_NONE;
    } else if (name.EqualsLiteral("session")) {
        if (value.EqualsLiteral("resource"))
            widget->session = Widget::WIDGET_SESSION_RESOURCE;
        else if (value.EqualsLiteral("site"))
            widget->session = Widget::WIDGET_SESSION_SITE;
    }
}

gcc_pure
static enum uri_base
parse_uri_base(StringView s)
{
    if (s.EqualsLiteral("widget"))
        return URI_BASE_WIDGET;
    else if (s.EqualsLiteral("child"))
        return URI_BASE_CHILD;
    else if (s.EqualsLiteral("parent"))
        return URI_BASE_PARENT;
    else
        return URI_BASE_TEMPLATE;
}

inline bool
XmlProcessor::LinkAttributeFinished(const XmlParserAttribute &attr)
{
    if (attr.name.EqualsLiteral("c:base")) {
        uri_rewrite.base = parse_uri_base(attr.value);

        if (tag != TAG_REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    if (attr.name.EqualsLiteral("c:mode")) {
        uri_rewrite.mode = parse_uri_mode(attr.value);

        if (tag != TAG_REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    if (attr.name.EqualsLiteral("c:view") &&
        attr.value.size < sizeof(uri_rewrite.view)) {
        memcpy(uri_rewrite.view,
               attr.value.data, attr.value.size);
        uri_rewrite.view[attr.value.size] = 0;

        if (tag != TAG_REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);

        return true;
    }

    if (attr.name.EqualsLiteral("xmlns:c")) {
        /* delete "xmlns:c" attributes */
        if (tag != TAG_REWRITE_URI)
            DeleteUriRewrite(attr.name_start, attr.end);
        return true;
    }

    return false;
}

static const char *
find_underscore(const char *p, const char *end)
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
XmlProcessor::HandleClassAttribute(const XmlParserAttribute &attr)
{
    auto p = attr.value.begin();
    const auto end = attr.value.end();

    const char *u = find_underscore(p, end);
    if (u == nullptr)
        return;

    expansible_buffer_reset(buffer);

    do {
        if (!expansible_buffer_write_buffer(buffer, p, u - p))
            return;

        p = u;

        const unsigned n = underscore_prefix(p, end);
        const char *prefix;
        if (n == 3 && (prefix = container->GetPrefix()) != nullptr) {
            if (!expansible_buffer_write_string(buffer, prefix))
                return;

            p += 3;
        } else if (n == 2 && (prefix = container->GetQuotedClassName()) != nullptr) {
            if (!expansible_buffer_write_string(buffer, prefix))
                return;

            p += 2;
        } else {
            /* failure; skip all underscores and find the next
               match */
            while (u < end && *u == '_')
                ++u;

            if (!expansible_buffer_write_buffer(buffer, p, u - p))
                return;

            p = u;
        }

        u = find_underscore(p, end);
    } while (u != nullptr);

    if (!expansible_buffer_write_buffer(buffer, p, end - p))
        return;

    const size_t length = expansible_buffer_length(buffer);
    void *q = expansible_buffer_dup(buffer, pool);
    ReplaceAttributeValue(attr, istream_memory_new(pool, q, length));
}

void
XmlProcessor::HandleIdAttribute(const XmlParserAttribute &attr)
{
    auto p = attr.value.begin();
    const auto end = attr.value.end();

    const unsigned n = underscore_prefix(p, end);
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = container->GetPrefix();
        if (prefix == nullptr)
            return;

        Replace(attr.value_start, attr.value_start + 3,
                istream_string_new(pool, prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name = container->GetQuotedClassName();
        if (class_name == nullptr)
            return;

        Replace(attr.value_start, attr.value_start + 2,
                istream_string_new(pool, class_name));
    }
}

void
XmlProcessor::HandleStyleAttribute(const XmlParserAttribute &attr)
{
    Istream *result =
        css_rewrite_block_uris(*pool,
                               *env,
                               *global_translate_cache,
                               *container,
                               attr.value,
                               &html_escape_class);
    if (result != nullptr)
        ReplaceAttributeValue(attr, result);
}

/**
 * Is this a tag which can have a link attribute?
 */
static bool
is_link_tag(enum tag tag)
{
    return tag == TAG_A || tag == TAG_FORM ||
         tag == TAG_IMG || tag == TAG_SCRIPT ||
        tag == TAG_META || tag == TAG_META_REFRESH ||
        tag == TAG_PARAM || tag == TAG_REWRITE_URI;
}

/**
 * Is this a HTML tag? (i.e. not a proprietary beng-proxy tag)
 */
static bool
is_html_tag(enum tag tag)
{
    return tag == TAG_OTHER || (is_link_tag(tag) && tag != TAG_REWRITE_URI);
}

void
XmlProcessor::OnXmlAttributeFinished(const XmlParserAttribute &attr)
{
    had_input = true;

    if (!IsQuiet() &&
        is_link_tag(tag) &&
        LinkAttributeFinished(attr))
        return;

    if (!IsQuiet() &&
        tag == TAG_META &&
        attr.name.EqualsLiteralIgnoreCase("http-equiv") &&
        attr.value.EqualsLiteralIgnoreCase("refresh")) {
        /* morph TAG_META to TAG_META_REFRESH */
        tag = TAG_META_REFRESH;
        return;
    }

    if (!IsQuiet() && HasOptionPrefixClass() &&
        /* due to a limitation in the processor and istream_replace,
           we cannot edit attributes followed by a URI attribute */
        !postponed_rewrite.pending &&
        is_html_tag(tag) &&
        attr.name.EqualsLiteral("class")) {
        HandleClassAttribute(attr);
        return;
    }

    if (!IsQuiet() &&
        HasOptionPrefixId() &&
        /* due to a limitation in the processor and istream_replace,
           we cannot edit attributes followed by a URI attribute */
        !postponed_rewrite.pending &&
        is_html_tag(tag) &&
        (attr.name.EqualsLiteral("id") ||
         attr.name.EqualsLiteral("for"))) {
        HandleIdAttribute(attr);
        return;
    }

    if (!IsQuiet() && HasOptionStyle() && HasOptionRewriteUrl() &&
        /* due to a limitation in the processor and istream_replace,
           we cannot edit attributes followed by a URI attribute */
        !postponed_rewrite.pending &&
        is_html_tag(tag) &&
        attr.name.EqualsLiteral("style")) {
        HandleStyleAttribute(attr);
        return;
    }

    switch (tag) {
    case TAG_NONE:
    case TAG_IGNORE:
    case TAG_OTHER:
        break;

    case TAG_WIDGET:
        assert(widget.widget != nullptr);

        parser_widget_attr_finished(widget.widget,
                                    attr.name, attr.value);
        break;

    case TAG_WIDGET_PARAM:
    case TAG_WIDGET_HEADER:
        assert(widget.widget != nullptr);

        if (attr.name.EqualsLiteral("name")) {
            expansible_buffer_set(widget.param.name, attr.value);
        } else if (attr.name.EqualsLiteral("value")) {
            expansible_buffer_set(widget.param.value, attr.value);
        }

        break;

    case TAG_WIDGET_PATH_INFO:
        assert(widget.widget != nullptr);

        if (attr.name.EqualsLiteral("value"))
            widget.widget->path_info
                = p_strdup(*widget.pool, attr.value);

        break;

    case TAG_WIDGET_VIEW:
        assert(widget.widget != nullptr);

        if (attr.name.EqualsLiteral("name")) {
            if (attr.value.IsEmpty()) {
                daemon_log(2, "empty view name\n");
                return;
            }

            widget.widget->view_name =
                p_strdup(*widget.pool, attr.value);
        }

        break;

    case TAG_IMG:
        if (attr.name.EqualsLiteralIgnoreCase("src"))
            PostponeUriRewrite(attr);
        break;

    case TAG_A:
        if (attr.name.EqualsLiteralIgnoreCase("href")) {
            if (!attr.value.StartsWith({"#", 1}) &&
                !attr.value.StartsWith({"javascript:", 11}))
                PostponeUriRewrite(attr);
        } else if (IsQuiet() &&
                   HasOptionPrefixId() &&
                   attr.name.EqualsLiteralIgnoreCase("name"))
            HandleIdAttribute(attr);

        break;

    case TAG_FORM:
        if (attr.name.EqualsLiteralIgnoreCase("action"))
            PostponeUriRewrite(attr);
        break;

    case TAG_SCRIPT:
        if (!IsQuiet() &&
            HasOptionRewriteUrl() &&
            attr.name.EqualsLiteralIgnoreCase("src"))
            PostponeUriRewrite(attr);
        break;

    case TAG_PARAM:
        if (attr.name.EqualsLiteral("value"))
            PostponeUriRewrite(attr);
        break;

    case TAG_META_REFRESH:
        if (attr.name.EqualsLiteralIgnoreCase("content"))
            PostponeRefreshRewrite(attr);
        break;

    case TAG_REWRITE_URI:
    case TAG_STYLE:
    case TAG_STYLE_PROCESS:
    case TAG_META:
        break;
    }
}

static GError *
widget_catch_callback(GError *error, void *ctx)
{
    auto *widget = (Widget *)ctx;

    daemon_log(3, "error from widget '%s': %s\n",
               widget->GetLogName(), error->message);
    g_error_free(error);
    return nullptr;
}

inline Istream *
XmlProcessor::EmbedWidget(Widget &child_widget)
{
    assert(child_widget.class_name != nullptr);

    if (replace != nullptr) {
        if (!widget_copy_from_request(child_widget, *env, nullptr) ||
            child_widget.display == Widget::WIDGET_DISPLAY_NONE) {
            widget_cancel(&child_widget);
            return nullptr;
        }

        Istream *istream = embed_inline_widget(*pool, *env, false,
                                               child_widget);
        if (istream != nullptr)
            istream = istream_catch_new(pool, *istream,
                                        widget_catch_callback, &child_widget);

        return istream;
    } else if (child_widget.id != nullptr &&
               strcmp(lookup_id, child_widget.id) == 0) {
        struct pool *const widget_pool = container->pool;
        auto &handler2 = *handler;

        parser_close(parser);
        parser = nullptr;

        GError *error = nullptr;
        if (!widget_copy_from_request(child_widget, *env, &error)) {
            widget_cancel(&child_widget);
            handler2.WidgetLookupError(error);
            pool_unref(widget_pool);
            pool_unref(caller_pool);
            return nullptr;
        }

        handler2.WidgetFound(child_widget);

        pool_unref(caller_pool);
        pool_unref(widget_pool);

        return nullptr;
    } else {
        widget_cancel(&child_widget);
        return nullptr;
    }
}

inline Istream *
XmlProcessor::OpenWidgetElement(Widget &child_widget)
{
    assert(child_widget.parent == container);

    if (child_widget.class_name == nullptr) {
        daemon_log(5, "widget without a class\n");
        return nullptr;
    }

    /* enforce the SELF_CONTAINER flag */
    const bool self_container =
        (options & PROCESSOR_SELF_CONTAINER) != 0;
    if (!widget_init_approval(&child_widget, self_container)) {
        daemon_log(5, "widget '%s' is not allowed to embed widget '%s'\n",
                   container->GetLogName(),
                   child_widget.GetLogName());
        return nullptr;
    }

    if (widget_check_recursion(child_widget.parent)) {
        daemon_log(5, "maximum widget depth exceeded for widget '%s'\n",
                   child_widget.GetLogName());
        return nullptr;
    }

    if (!expansible_buffer_is_empty(widget.params))
        child_widget.query_string = expansible_buffer_strdup(widget.params,
                                                              widget.pool);

    container->children.push_front(child_widget);

    return EmbedWidget(child_widget);
}

inline void
XmlProcessor::WidgetElementFinished(const XmlParserTag &widget_tag,
                                    Widget &child_widget)
{
    Istream *istream = OpenWidgetElement(child_widget);
    assert(istream == nullptr || replace != nullptr);

    if (replace != nullptr)
        Replace(widget.start_offset, widget_tag.end, istream);
}

static bool
header_name_valid(const char *name, size_t length)
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
expansible_buffer_append_uri_escaped(struct expansible_buffer *buffer,
                                     const char *value, size_t length)
{
    char *escaped = (char *)p_malloc(tpool, length * 3);
    length = uri_escape(escaped, StringView(value, length));
    expansible_buffer_write_buffer(buffer, escaped, length);
}

void
XmlProcessor::OnXmlTagFinished(const XmlParserTag &xml_tag)
{
    had_input = true;

    if (postponed_rewrite.pending)
        CommitUriRewrite();

    if (tag == TAG_WIDGET) {
        if (xml_tag.type == XmlParserTagType::OPEN || xml_tag.type == XmlParserTagType::SHORT)
            widget.start_offset = xml_tag.start;
        else if (widget.widget == nullptr)
            return;

        assert(widget.widget != nullptr);

        if (xml_tag.type == XmlParserTagType::OPEN)
            return;

        auto &child_widget = *widget.widget;
        widget.widget = nullptr;

        WidgetElementFinished(xml_tag, child_widget);
    } else if (tag == TAG_WIDGET_PARAM) {
        assert(widget.widget != nullptr);

        if (expansible_buffer_is_empty(widget.param.name))
            return;

        const AutoRewindPool auto_rewind(*tpool);

        size_t length;
        const char *p = (const char *)
            expansible_buffer_read(widget.param.value, &length);
        if (memchr(p, '&', length) != nullptr) {
            char *q = (char *)p_memdup(tpool, p, length);
            length = unescape_inplace(&html_escape_class, q, length);
            p = q;
        }

        if (!expansible_buffer_is_empty(widget.params))
            expansible_buffer_write_buffer(widget.params, "&", 1);

        size_t name_length;
        const char *name = (const char *)
            expansible_buffer_read(widget.param.name, &name_length);

        expansible_buffer_append_uri_escaped(widget.params,
                                             name, name_length);

        expansible_buffer_write_buffer(widget.params, "=", 1);

        expansible_buffer_append_uri_escaped(widget.params,
                                             p, length);
    } else if (tag == TAG_WIDGET_HEADER) {
        assert(widget.widget != nullptr);

        if (xml_tag.type == XmlParserTagType::CLOSE)
            return;

        size_t length;
        const char *name = (const char *)
            expansible_buffer_read(widget.param.name, &length);
        if (!header_name_valid(name, length)) {
            daemon_log(3, "invalid widget HTTP header name\n");
            return;
        }

        if (widget.widget->headers == nullptr)
            widget.widget->headers = strmap_new(widget.pool);

        char *value = expansible_buffer_strdup(widget.param.value,
                                               widget.pool);
        if (strchr(value, '&') != nullptr) {
            length = unescape_inplace(&html_escape_class,
                                      value, strlen(value));
            value[length] = 0;
        }

        widget.widget->headers->Add(expansible_buffer_strdup(widget.param.name,
                                                             widget.pool),
                                    value);
    } else if (tag == TAG_SCRIPT) {
        if (xml_tag.type == XmlParserTagType::OPEN)
            parser_script(parser);
        else
            tag = TAG_NONE;
    } else if (tag == TAG_REWRITE_URI) {
        /* the settings of this tag become the new default */
        default_uri_rewrite = uri_rewrite;

        Replace(xml_tag.start, xml_tag.end, nullptr);
    } else if (tag == TAG_STYLE) {
        if (xml_tag.type == XmlParserTagType::OPEN && !IsQuiet() && HasOptionStyle()) {
            /* create a CSS processor for the contents of this style
               element */

            tag = TAG_STYLE_PROCESS;

            unsigned css_options = 0;
            if (options & PROCESSOR_REWRITE_URL)
                css_options |= CSS_PROCESSOR_REWRITE_URL;
            if (options & PROCESSOR_PREFIX_CSS_CLASS)
                css_options |= CSS_PROCESSOR_PREFIX_CLASS;
            if (options & PROCESSOR_PREFIX_XML_ID)
                css_options |= CSS_PROCESSOR_PREFIX_ID;

            Istream *istream =
                css_processor(*pool, *StartCdataIstream(),
                              *container, *env,
                              css_options);

            /* the end offset will be extended later with
               istream_replace_extend() */
            cdata_start = xml_tag.end;
            Replace(xml_tag.end, xml_tag.end, istream);
        }
    }
}

size_t
XmlProcessor::OnXmlCdata(const char *p gcc_unused, size_t length,
                         gcc_unused bool escaped, off_t start)
{
    had_input = true;

    if (tag == TAG_STYLE_PROCESS) {
        /* XXX unescape? */
        length = cdata_istream->InvokeData(p, length);
        if (length > 0)
            istream_replace_extend(*replace, cdata_start, start + length);
    } else if (replace != nullptr && widget.widget == nullptr)
        istream_replace_settle(*replace, start + length);

    return length;
}

void
XmlProcessor::OnXmlEof(gcc_unused off_t length)
{
    struct pool *const widget_pool = container->pool;

    assert(parser != nullptr);

    parser = nullptr;

    StopCdataIstream();

    if (container->for_focused.body != nullptr)
        /* the request body could not be submitted to the focused
           widget, because we didn't find it; dispose it now */
        istream_free_unused(&container->for_focused.body);

    if (replace != nullptr)
        istream_replace_finish(*replace);

    if (lookup_id != nullptr) {
        /* widget was not found */
        async.Finished();

        handler->WidgetNotFound();
        pool_unref(caller_pool);
    }

    pool_unref(widget_pool);
}

void
XmlProcessor::OnXmlError(GError *error)
{
    struct pool *const widget_pool = container->pool;

    assert(parser != nullptr);

    parser = nullptr;

    StopCdataIstream();

    if (container->for_focused.body != nullptr)
        /* the request body could not be submitted to the focused
           widget, because we didn't find it; dispose it now */
        istream_free_unused(&container->for_focused.body);

    if (lookup_id != nullptr) {
        async.Finished();
        handler->WidgetLookupError(error);
        pool_unref(caller_pool);
    } else
        g_error_free(error);

    pool_unref(widget_pool);
}

static void
processor_parser_init(XmlProcessor &processor, Istream &input)
{
    processor.parser = parser_new(*processor.pool, input,
                                  processor);
}
