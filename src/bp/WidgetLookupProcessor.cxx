// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "WidgetLookupProcessor.hxx"
#include "WidgetContainerParser.hxx"
#include "XmlProcessor.hxx"
#include "parser/XmlParser.hxx"
#include "widget/Widget.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/Context.hxx"
#include "widget/Error.hxx"
#include "istream/Sink.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "util/DestructObserver.hxx"
#include "util/Cancellable.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <string.h>

class WidgetLookupProcessor final : IstreamSink, WidgetContainerParser, Cancellable, DestructAnchor {
	const StopwatchPtr stopwatch;

	const char *const lookup_id;
	const unsigned options;

	XmlParser parser;
	bool had_input;

	WidgetLookupHandler &handler;

public:
	WidgetLookupProcessor(struct pool &pool, const StopwatchPtr &parent_stopwatch,
			      UnusedIstreamPtr &&_input,
			      Widget &_widget, SharedPoolPtr<WidgetContext> &&_ctx,
			      unsigned _options,
			      const char *_lookup_id,
			      WidgetLookupHandler &_handler,
			      CancellablePointer &caller_cancel_ptr) noexcept
		:IstreamSink(std::move(_input)),
		 WidgetContainerParser(pool, _widget, std::move(_ctx)),
		 stopwatch(parent_stopwatch, "WidgetLookupProcessor"),
		 lookup_id(_lookup_id),
		 options(_options),
		 parser(pool, *this),
		 handler(_handler)
	{
		caller_cancel_ptr = *this;
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

private:
	void Destroy() noexcept {
		this->~WidgetLookupProcessor();
	}

	void Close() noexcept {
		Destroy();
	}

	/**
	 * Throws an exception if the widget is not allowed here.
	 */
	Widget &PrepareEmbedWidget(WidgetPtr &&child_widget);

	void FoundWidget(WidgetPtr &&child_widget) noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		return parser.Feed((const char *)src.data(), src.size());
	}

	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class WidgetContainerParser */
	bool WantWidget(const Widget &w) const noexcept override {
		return w.id != nullptr && StringIsEqual(w.id, lookup_id);
	}

	bool WidgetElementFinished(const XmlParserTag &tag,
				   WidgetPtr &&child_widget) noexcept override;

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &tag) noexcept override;
	bool OnXmlTagFinished(const XmlParserTag &tag) noexcept override;
	void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override;
	size_t OnXmlCdata(std::string_view text, bool escaped,
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

/*
 * async operation
 *
 */

void
WidgetLookupProcessor::Cancel() noexcept
{
	stopwatch.RecordEvent("cancel");

	/* the request body was not yet submitted to the focused widget;
	   dispose it now */
	container.DiscardForFocused();

	Close();
}

/*
 * parser callbacks
 *
 */

bool
WidgetLookupProcessor::OnXmlTagStart(const XmlParserTag &xml_tag) noexcept
{
	had_input = true;

	return WidgetContainerParser::OnXmlTagStart(xml_tag);
}

void
WidgetLookupProcessor::OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept
{
	had_input = true;

	WidgetContainerParser::OnXmlAttributeFinished(attr);
}

inline Widget &
WidgetLookupProcessor::PrepareEmbedWidget(WidgetPtr &&child_widget)
{
	if (child_widget->class_name == nullptr)
		throw std::runtime_error("widget without a class");

	/* enforce the SELF_CONTAINER flag */
	const bool self_container =
		(options & PROCESSOR_SELF_CONTAINER) != 0;
	if (!child_widget->InitApproval(self_container))
		throw FmtRuntimeError("widget is not allowed to embed widget '{}'",
				      child_widget->GetLogName());

	if (widget_check_recursion(child_widget->parent))
		throw FmtRuntimeError("maximum widget depth exceeded for widget '{}'",
				      child_widget->GetLogName());

	if (!widget.params.IsEmpty())
		child_widget->from_template.query_string =
			widget.params.StringDup(widget.pool);

	container.children.push_front(*child_widget.release());
	return container.children.front();
}

inline void
WidgetLookupProcessor::FoundWidget(WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->parent == &container);

	auto &handler2 = handler;

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

bool
WidgetLookupProcessor::WidgetElementFinished(const XmlParserTag &,
					  WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->id != nullptr);
	assert(StringIsEqual(child_widget->id, lookup_id));

	FoundWidget(std::move(child_widget));
	return false;
}

bool
WidgetLookupProcessor::OnXmlTagFinished(const XmlParserTag &xml_tag) noexcept
{
	had_input = true;

	return WidgetContainerParser::OnXmlTagFinished(xml_tag);
}

size_t
WidgetLookupProcessor::OnXmlCdata(std::string_view text, bool, off_t) noexcept
{
	had_input = true;

	return text.size();
}

void
WidgetLookupProcessor::OnEof() noexcept
{
	input.Clear();

	stopwatch.RecordEvent("eof");

	/* the request body could not be submitted to the focused widget,
	   because we didn't find it; dispose it now */
	container.DiscardForFocused();

	/* widget was not found */

	auto &_handler = handler;
	Destroy();
	_handler.WidgetNotFound();
}

void
WidgetLookupProcessor::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();

	stopwatch.RecordEvent("error");

	/* the request body could not be submitted to the focused widget,
	   because we didn't find it; dispose it now */
	container.DiscardForFocused();

	auto &_handler = handler;
	Destroy();
	_handler.WidgetLookupError(ep);
}

void
processor_lookup_widget(struct pool &pool,
			const StopwatchPtr &parent_stopwatch,
			UnusedIstreamPtr istream,
			Widget &widget, const char *id,
			SharedPoolPtr<WidgetContext> ctx,
			unsigned options,
			WidgetLookupHandler &handler,
			CancellablePointer &cancel_ptr) noexcept
{
	assert(id != nullptr);

	if ((options & PROCESSOR_CONTAINER) == 0) {
		auto e = WidgetError(WidgetErrorCode::NOT_A_CONTAINER,
				     "Not a container");
		handler.WidgetLookupError(std::make_exception_ptr(e));
		return;
	}

	auto *processor =
		NewFromPool<WidgetLookupProcessor>(pool, pool, parent_stopwatch,
						   std::move(istream),
						   widget, std::move(ctx), options,
						   id, handler,
						   cancel_ptr);
	processor->ReadLoop();
}
