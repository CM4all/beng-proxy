// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "widget/Ptr.hxx"
#include "parser/XmlParser.hxx"
#include "pool/SharedPtr.hxx"

class Widget;
struct WidgetContext;

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
	virtual bool WantWidget(const Widget &w) const noexcept = 0;

	virtual bool WidgetElementFinished(const XmlParserTag &widget_tag,
					   WidgetPtr &&child_widget) noexcept = 0;

	virtual bool OnProcessingInstruction(std::string_view name) noexcept {
		(void)name;
		return false;
	}

	virtual bool OnXmlTagStart2(const XmlParserTag &xml_tag) noexcept {
		(void)xml_tag;
		return false;
	}

private:
	void CancelWidget() noexcept;

	bool OnStartElementInWidget(XmlParserTagType type,
				    std::string_view name) noexcept;

public:
	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &tag) noexcept override;
	bool OnXmlTagFinished(const XmlParserTag &tag) noexcept override;
	void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override;
};
