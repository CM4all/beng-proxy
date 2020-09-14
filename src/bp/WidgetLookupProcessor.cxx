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
#include "util/DestructObserver.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"
#include "util/Cancellable.hxx"
#include "util/ScopeExit.hxx"
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
	bool CheckWidgetLookup(WidgetPtr &&child_widget) noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override {
		return parser.Feed((const char *)data, length);
	}

	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class WidgetContainerParser */
	bool WidgetElementFinished(const XmlParserTag &tag,
				   WidgetPtr &&child_widget) noexcept override;

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

inline bool
WidgetLookupProcessor::CheckWidgetLookup(WidgetPtr &&child_widget) noexcept
{
	assert(child_widget->parent == &container);

	if (child_widget->id != nullptr &&
	    strcmp(lookup_id, child_widget->id) == 0) {
		FoundWidget(std::move(child_widget));
		return false;
	} else {
		child_widget->Cancel();
		return true;
	}
}

bool
WidgetLookupProcessor::WidgetElementFinished(const XmlParserTag &,
					  WidgetPtr &&child_widget) noexcept
{
	return CheckWidgetLookup(std::move(child_widget));
}

bool
WidgetLookupProcessor::OnXmlTagFinished(const XmlParserTag &xml_tag) noexcept
{
	had_input = true;

	return WidgetContainerParser::OnXmlTagFinished(xml_tag);
}

size_t
WidgetLookupProcessor::OnXmlCdata(StringView text, bool, off_t) noexcept
{
	had_input = true;

	return text.size;
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
