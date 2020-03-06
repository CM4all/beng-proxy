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

#include "WidgetContainerParser.hxx"
#include "widget/Context.hxx"
#include "widget/Widget.hxx"
#include "pool/tpool.hxx"
#include "uri/Escape.hxx"
#include "util/CharUtil.hxx"
#include "escape_class.hxx"
#include "escape_html.hxx"
#include "strmap.hxx"

WidgetContainerParser::WidgetContainerParser(struct pool &_pool,
					     Widget &_container,
					     SharedPoolPtr<WidgetContext> &&_ctx) noexcept
	:container(_container), ctx(std::move(_ctx)),
	 widget(container.pool, _pool) {}

inline bool
WidgetContainerParser::OnStartElementInWidget(XmlParserTagType type,
					      StringView name) noexcept
{
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
WidgetContainerParser::OnXmlTagStart(const XmlParserTag &xml_tag) noexcept
{
	if (tag == Tag::SCRIPT && !xml_tag.name.EqualsIgnoreCase("script"))
		/* workaround for bugged scripts: ignore all closing tags
		   except </SCRIPT> */
		return false;

	tag = Tag::IGNORE;

	if (xml_tag.type == XmlParserTagType::PI)
		return OnProcessingInstruction(xml_tag.name);

	if (widget.widget != nullptr)
		return OnStartElementInWidget(xml_tag.type, xml_tag.name);

	if (OnXmlTagStart2(xml_tag)) {
		return true;
	} else if (xml_tag.name.Equals("c:widget")) {
		if (ctx->widget_registry == nullptr)
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
		tag = Tag::SCRIPT;
		return true;
	} else {
		return false;
	}
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

void
WidgetContainerParser::OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept
{
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
	case Tag::A:
	case Tag::FORM:
	case Tag::SCRIPT:
	case Tag::PARAM:
	case Tag::META_REFRESH:
	case Tag::META_URI_CONTENT:
	case Tag::REWRITE_URI:
	case Tag::STYLE:
	case Tag::STYLE_PROCESS:
	case Tag::META:
		break;
	}
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
WidgetContainerParser::OnXmlTagFinished(const XmlParserTag &xml_tag) noexcept
{
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
		tag = Tag::NONE;
	}

	return true;
}
