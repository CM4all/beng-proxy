/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "parser/XmlParser.hxx"
#include "parser/CssSyntax.hxx"
#include "parser/CssUtil.hxx"
#include "uri/Escape.hxx"
#include "uri/Extract.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "widget/Error.hxx"
#include "widget/Inline.hxx"
#include "widget/RewriteUri.hxx"
#include "pool/tpool.hxx"
#include "expansible_buffer.hxx"
#include "escape_class.hxx"
#include "escape_html.hxx"
#include "strmap.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream.hxx"
#include "istream/Sink.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/istream_catch.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"
#include "istream/TeeIstream.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "util/CharUtil.hxx"
#include "util/DestructObserver.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"
#include "util/Cancellable.hxx"
#include "util/ScopeExit.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <string.h>

enum class UriBase {
	TEMPLATE,
	WIDGET,
	CHILD,
	PARENT,
};

struct UriRewrite {
	UriBase base;
	RewriteUriMode mode;

	char view[64];
};

struct XmlProcessor final : PoolHolder, IstreamSink, XmlParserHandler, Cancellable, DestructAnchor {
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

	const StopwatchPtr stopwatch;

	Widget &container;
	const char *lookup_id;
	const SharedPoolPtr<WidgetContext> ctx;
	const unsigned options;

	SharedPoolPtr<ReplaceIstreamControl> replace;

	XmlParser parser;
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

	UriRewrite uri_rewrite;

	/**
	 * The default value for #uri_rewrite.
	 */
	UriRewrite default_uri_rewrite;

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
		WidgetPtr widget;

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

	XmlProcessor(PoolPtr &&_pool, const StopwatchPtr &parent_stopwatch,
		     UnusedIstreamPtr &&_input,
		     Widget &_widget, SharedPoolPtr<WidgetContext> &&_ctx,
		     unsigned _options,
		     SharedPoolPtr<ReplaceIstreamControl> &&_replace) noexcept
		:PoolHolder(std::move(_pool)), IstreamSink(std::move(_input)),
		 stopwatch(parent_stopwatch, "XmlProcessor"),
		 container(_widget),
		 lookup_id(nullptr),
		 ctx(std::move(_ctx)), options(_options),
		 replace(std::move(_replace)),
		 parser(pool, *this),
		 buffer(pool, 128, 2048),
		 postponed_rewrite(pool),
		 widget(_widget.pool, pool)
	{
		if (HasOptionRewriteUrl()) {
			default_uri_rewrite.base = UriBase::TEMPLATE;
			default_uri_rewrite.mode = RewriteUriMode::PARTIAL;
			default_uri_rewrite.view[0] = 0;

			if (options & PROCESSOR_FOCUS_WIDGET) {
				default_uri_rewrite.base = UriBase::WIDGET;
				default_uri_rewrite.mode = RewriteUriMode::FOCUS;
			}
		}
	}

	XmlProcessor(PoolPtr &&_pool, const StopwatchPtr &parent_stopwatch,
		     UnusedIstreamPtr &&_input,
		     Widget &_widget, SharedPoolPtr<WidgetContext> &&_ctx,
		     unsigned _options,
		     const char *_lookup_id,
		     WidgetLookupHandler &_handler,
		     CancellablePointer &caller_cancel_ptr) noexcept
		:PoolHolder(std::move(_pool)), IstreamSink(std::move(_input)),
		 stopwatch(parent_stopwatch, "XmlProcessor"),
		 container(_widget),
		 lookup_id(_lookup_id),
		 ctx(std::move(_ctx)), options(_options),
		 parser(pool, *this),
		 buffer(pool, 128, 2048),
		 postponed_rewrite(pool),
		 widget(_widget.pool, pool),
		 handler(&_handler), cancel_ptr(&caller_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	struct pool &GetPool() const noexcept {
		return pool;
	}

	void Read() noexcept {
		input.Read();
	}

	void ReadLoop() noexcept {
		const DestructObserver destructed(*this);

		do {
			had_input = false;
			Read();
		} while (!destructed && had_input);
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

	bool MustRewriteEmptyURI() const noexcept {
		return tag == Tag::FORM;
	}

	void Destroy() noexcept {
		this->~XmlProcessor();
	}

	void Close() noexcept {
		ClearAndCloseInput();
		Destroy();
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
				StringView value) noexcept;

	void PostponeUriRewrite(const XmlParserAttribute &attr) noexcept {
		PostponeUriRewrite(attr.value_start, attr.value_end, attr.value);
	}

	void PostponeRefreshRewrite(const XmlParserAttribute &attr) noexcept;

	void CommitUriRewrite() noexcept;

	void DeleteUriRewrite(off_t start, off_t end) noexcept;

	void TransformUriAttribute(const XmlParserAttribute &attr,
				   UriBase base, RewriteUriMode mode,
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
	Widget &PrepareEmbedWidget(WidgetPtr &&child_widget);

	UnusedIstreamPtr EmbedWidget(Widget &child_widget) noexcept;
	UnusedIstreamPtr OpenWidgetElement(WidgetPtr &&child_widget) noexcept;
	void FoundWidget(WidgetPtr &&child_widget) noexcept;
	bool CheckWidgetLookup(WidgetPtr &&child_widget) noexcept;
	bool WidgetElementFinished(const XmlParserTag &tag,
				   WidgetPtr &&child_widget) noexcept;

	Istream *StartCdataIstream() noexcept;
	void StopCdataIstream() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override {
		return parser.Feed((const char *)data, length);
	}

	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &tag) noexcept override;
	bool OnXmlTagFinished(const XmlParserTag &tag) noexcept override;
	void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override;
	size_t OnXmlCdata(StringView text, bool escaped,
			  off_t start) noexcept override;

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

/**
 * @param rewrite_empty rewrite empty URIs?  This is not always
 * necessary, but definitely is for form actions.
 */
gcc_pure
static bool
CanRewriteUri(StringView uri, bool rewrite_empty) noexcept
{
	if (uri.empty())
		/* an empty URI is a reference to the current document and
		   thus should be rewritten */
		return rewrite_empty;

	if (uri.front() == '#')
		/* can't rewrite URI fragments */
		return false;

	if (uri.StartsWith("data:") ||
	    uri.StartsWith("mailto:") || uri.StartsWith("javascript:"))
		/* ignore data, email and JavaScript links */
		return false;

	if (uri_has_authority(uri))
		/* can't rewrite if the specified URI is absolute */
		return false;

	return true;
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
	container.DiscardForFocused();

	Close();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
processor_process(struct pool &caller_pool,
		  const StopwatchPtr &parent_stopwatch,
		  UnusedIstreamPtr input,
		  Widget &widget,
		  SharedPoolPtr<WidgetContext> ctx,
		  unsigned options)
{
	auto pool = pool_new_linear(&caller_pool, "XmlProcessor", 32768);

	/* the text processor will expand entities */
	auto tee = NewTeeIstream(pool,
				 text_processor(pool,
						std::move(input),
						widget, *ctx),
				 ctx->event_loop,
				 true);

	auto tee2 = AddTeeIstream(tee, true);

	auto r = istream_replace_new(ctx->event_loop, pool,
				     std::move(tee));
	NewFromPool<XmlProcessor>(std::move(pool), parent_stopwatch,
				  std::move(tee2),
				  widget, std::move(ctx), options,
				  std::move(r.second));

	//XXX headers = processor_header_forward(pool, headers);
	return std::move(r.first);
}

void
processor_lookup_widget(struct pool &caller_pool,
			const StopwatchPtr &parent_stopwatch,
			UnusedIstreamPtr istream,
			Widget &widget, const char *id,
			SharedPoolPtr<WidgetContext> ctx,
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

	auto pool = pool_new_linear(&caller_pool, "XmlProcessor", 32768);
	auto *processor =
		NewFromPool<XmlProcessor>(std::move(pool), parent_stopwatch,
					  std::move(istream),
					  widget, std::move(ctx), options,
					  id, handler,
					  cancel_ptr);
	processor->ReadLoop();
}

void
XmlProcessor::PostponeUriRewrite(off_t start, off_t end,
				 StringView value) noexcept
{
	assert(start <= end);

	if (postponed_rewrite.pending)
		/* cannot rewrite more than one attribute per element */
		return;

	if (!CanRewriteUri(value, MustRewriteEmptyURI()))
		return;

	/* postpone the URI rewrite until the tag is finished: save the
	   attribute value position, save the original attribute value and
	   set the "pending" flag */

	postponed_rewrite.uri_start = start;
	postponed_rewrite.uri_end = end;

	bool success = postponed_rewrite.value.Set(value);

	for (auto &i : postponed_rewrite.delete_)
		i.start = 0;

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
		if (i >= std::size(postponed_rewrite.delete_))
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
			   attr.value_end - 1, {p, end - 1});
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

	for (const auto &i : postponed_rewrite.delete_)
		if (i.start > 0)
			Replace(i.start, i.end, nullptr);
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

	processor.Read();
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

	name.SkipPrefix("c:");

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
		    ctx->widget_registry == nullptr)
			return false;

		if (xml_tag.type == XmlParserTagType::CLOSE) {
			assert(widget.widget == nullptr);
			return false;
		}

		tag = Tag::WIDGET;
		widget.widget = MakeWidget(widget.pool, nullptr);
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
				    UriBase base,
				    RewriteUriMode mode,
				    const char *view) noexcept
{
	StringView value = attr.value;

	/* this has been checked already by PostponeUriRewrite() */
	assert(CanRewriteUri(value, MustRewriteEmptyURI()));

	Widget *target_widget = nullptr;
	StringView child_id, suffix;

	switch (base) {
	case UriBase::TEMPLATE:
		/* no need to rewrite the attribute */
		return;

	case UriBase::WIDGET:
		target_widget = &container;
		break;

	case UriBase::CHILD:
		SplitString(value, '/', child_id, suffix);

		target_widget = container.FindChild(p_strdup(pool, child_id));
		if (target_widget == nullptr)
			return;

		value = suffix;
		break;

	case UriBase::PARENT:
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
		rewrite_widget_uri(pool, ctx, stopwatch,
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
parser_widget_attr_finished(Widget &widget,
			    StringView name, StringView value)
{
	if (name.Equals("type")) {
		if (value.empty())
			throw std::runtime_error("empty widget class name");

		widget.SetClassName(value);
	} else if (name.Equals("id")) {
		if (!value.empty())
			widget.SetId(value);
	} else if (name.Equals("display")) {
		if (value.Equals("inline"))
			widget.display = Widget::Display::INLINE;
		else if (value.Equals("none"))
			widget.display = Widget::Display::NONE;
		else
			throw std::runtime_error("Invalid widget 'display' attribute");
	} else if (name.Equals("session")) {
		if (value.Equals("resource"))
			widget.session_scope = Widget::SessionScope::RESOURCE;
		else if (value.Equals("site"))
			widget.session_scope = Widget::SessionScope::SITE;
		else
			throw std::runtime_error("Invalid widget 'session' attribute");
	}
}

gcc_pure
static UriBase
parse_uri_base(StringView s) noexcept
{
	if (s.Equals("widget"))
		return UriBase::WIDGET;
	else if (s.Equals("child"))
		return UriBase::CHILD;
	else if (s.Equals("parent"))
		return UriBase::PARENT;
	else
		return UriBase::TEMPLATE;
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

	if (is_underscore_prefix({p, end}))
		return p;

	while (true) {
		p = (const char *)memchr(p + 1, '_', end - p);
		if (p == nullptr)
			return nullptr;

		if (IsWhitespaceOrNull(p[-1]) &&
		    is_underscore_prefix({p, end}))
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

		const unsigned n = underscore_prefix({p, end});
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

	const unsigned n = underscore_prefix({p, end});
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
		css_rewrite_block_uris(pool, ctx, stopwatch,
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

		try {
			parser_widget_attr_finished(*widget.widget,
						    attr.name, attr.value);
		} catch (...) {
			container.logger(2, std::current_exception());
			// TODO: discard errored widget?
		}

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
	const auto &widget = *(const Widget *)ctx;

	widget.logger(3, ep);
	return {};
}

inline Widget &
XmlProcessor::PrepareEmbedWidget(WidgetPtr &&child_widget)
{
	if (child_widget->class_name == nullptr)
		throw std::runtime_error("widget without a class");

	/* enforce the SELF_CONTAINER flag */
	const bool self_container =
		(options & PROCESSOR_SELF_CONTAINER) != 0;
	if (!child_widget->InitApproval(self_container))
		throw FormatRuntimeError("widget is not allowed to embed widget '%s'",
					 child_widget->GetLogName());

	if (widget_check_recursion(child_widget->parent))
		throw FormatRuntimeError("maximum widget depth exceeded for widget '%s'",
					 child_widget->GetLogName());

	if (!widget.params.IsEmpty())
		child_widget->from_template.query_string =
			widget.params.StringDup(widget.pool);

	container.children.push_front(*child_widget.release());
	return container.children.front();
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

	StopwatchPtr widget_stopwatch(stopwatch, "widget ",
				      child_widget.class_name);

	auto istream = embed_inline_widget(pool, ctx, widget_stopwatch,
					   false, child_widget);
	if (istream)
		istream = istream_catch_new(pool, std::move(istream),
					    widget_catch_callback, &child_widget);

	return istream;
}

inline UnusedIstreamPtr
XmlProcessor::OpenWidgetElement(WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->parent == &container);

	try {
		return EmbedWidget(PrepareEmbedWidget(std::move(child_widget)));
	} catch (...) {
		container.logger(5, std::current_exception());
		return nullptr;
	}
}

inline void
XmlProcessor::FoundWidget(WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->parent == &container);
	assert(!replace);

	auto &handler2 = *handler;

	Widget *child_widget2 = nullptr;

	try {
		{
			AtScopeExit(this) { Close(); };
			child_widget2 = &PrepareEmbedWidget(std::move(child_widget));
		}

		child_widget2->CopyFromRequest();
		handler2.WidgetFound(*child_widget2);
	} catch (...) {
		if (child_widget2 != nullptr)
			child_widget2->Cancel();
		handler2.WidgetLookupError(std::current_exception());
	}
}

inline bool
XmlProcessor::CheckWidgetLookup(WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->parent == &container);
	assert(!replace);

	if (child_widget->id != nullptr &&
	    strcmp(lookup_id, child_widget->id) == 0) {
		FoundWidget(std::move(child_widget));
		return false;
	} else {
		child_widget->Cancel();
		return true;
	}
}

inline bool
XmlProcessor::WidgetElementFinished(const XmlParserTag &widget_tag,
				    WidgetPtr &&child_widget) noexcept
{
	if (replace) {
		Replace(widget.start_offset, widget_tag.end,
			OpenWidgetElement(std::move(child_widget)));
		return true;
	} else
		return CheckWidgetLookup(std::move(child_widget));
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
				     struct pool &tpool,
				     StringView value) noexcept
{
	char *escaped = (char *)p_malloc(&tpool, value.size * 3);
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

		return WidgetElementFinished(xml_tag,
					     std::exchange(widget.widget,
							   nullptr));
	} else if (tag == Tag::WIDGET_PARAM) {
		assert(widget.widget != nullptr);

		if (widget.param.name.IsEmpty())
			return true;

		const TempPoolLease tpool;

		auto value = widget.param.value.ReadStringView();
		if (value.Find('&') != nullptr) {
			char *q = (char *)p_memdup(tpool, value.data, value.size);
			value.size = unescape_inplace(&html_escape_class, q, value.size);
			value.data = q;
		}

		if (!widget.params.IsEmpty())
			widget.params.Write("&", 1);

		const auto name = widget.param.name.ReadStringView();
		expansible_buffer_append_uri_escaped(widget.params, tpool, name);

		widget.params.Write("=", 1);

		expansible_buffer_append_uri_escaped(widget.params, tpool, value);
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

		widget.widget->from_template.headers->Add(widget.pool,
							  widget.param.name.StringDup(widget.pool),
							  value);
	} else if (tag == Tag::SCRIPT) {
		if (xml_tag.type == XmlParserTagType::OPEN)
			parser.Script();
		else
			tag = Tag::NONE;
	} else if (tag == Tag::REWRITE_URI) {
		/* the settings of this tag become the new default */
		default_uri_rewrite = uri_rewrite;

		Replace(xml_tag.start, xml_tag.end, nullptr);
	} else if (tag == Tag::STYLE) {
		if (xml_tag.type == XmlParserTagType::OPEN && !IsQuiet() &&
		    HasOptionStyle()) {
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
				css_processor(pool, stopwatch,
					      UnusedIstreamPtr(StartCdataIstream()),
					      container, ctx,
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
XmlProcessor::OnXmlCdata(StringView text,
			 gcc_unused bool escaped, off_t start) noexcept
{
	had_input = true;

	if (tag == Tag::STYLE_PROCESS) {
		/* XXX unescape? */
		size_t length = cdata_istream->InvokeData(text.data, text.size);
		if (length > 0)
			replace->Extend(cdata_start, start + length);
	} else if (replace && widget.widget == nullptr)
		replace->Settle(start + text.size);

	return text.size;
}

void
XmlProcessor::OnEof() noexcept
{
	input.Clear();

	StopCdataIstream();

	/* the request body could not be submitted to the focused widget,
	   because we didn't find it; dispose it now */
	container.DiscardForFocused();

	if (lookup_id != nullptr) {
		/* widget was not found */

		assert(!replace);

		auto &_handler = *handler;
		Destroy();
		_handler.WidgetNotFound();
	} else {
		assert(replace);
		auto _replace = std::move(replace);

		Destroy();

		_replace->Finish();
	}
}

void
XmlProcessor::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();

	StopCdataIstream();

	/* the request body could not be submitted to the focused widget,
	   because we didn't find it; dispose it now */
	container.DiscardForFocused();

	if (lookup_id != nullptr) {
		auto &_handler = *handler;
		Destroy();
		_handler.WidgetLookupError(ep);
	} else
		Destroy();
}
