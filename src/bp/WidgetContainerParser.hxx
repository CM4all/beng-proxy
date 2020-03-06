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

#pragma once

#include "widget/Ptr.hxx"
#include "parser/XmlParser.hxx"
#include "pool/SharedPtr.hxx"

class Widget;
class WidgetContext;

class WidgetContainerParser : public XmlParserHandler {
protected:
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

	Widget &container;
	const SharedPoolPtr<WidgetContext> ctx;

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

public:
	WidgetContainerParser(struct pool &_pool, Widget &_container,
			      SharedPoolPtr<WidgetContext> &&_ctx) noexcept;

protected:
	virtual bool WidgetElementFinished(const XmlParserTag &widget_tag,
					   WidgetPtr &&child_widget) noexcept = 0;

	virtual bool OnProcessingInstruction(StringView name) noexcept {
		(void)name;
		return false;
	}

	virtual bool OnXmlTagStart2(const XmlParserTag &xml_tag) noexcept {
		(void)xml_tag;
		return false;
	}

private:
	bool OnStartElementInWidget(XmlParserTagType type,
				    StringView name) noexcept;

public:
	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &tag) noexcept override;
	bool OnXmlTagFinished(const XmlParserTag &tag) noexcept override;
	void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override;
};
