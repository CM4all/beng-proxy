// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "WidgetContainerParser.hxx"
#include "widget/Context.hxx"
#include "widget/Widget.hxx"
#include "pool/tpool.hxx"
#include "uri/Escape.hxx"
#include "util/CharUtil.hxx"
#include "util/StringCompare.hxx"
#include "escape/Class.hxx"
#include "escape/HTML.hxx"
#include "escape/Pool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <stdexcept>

using std::string_view_literals::operator""sv;

WidgetContainerParser::WidgetContainerParser(struct pool &_pool,
					     Widget &_container,
					     SharedPoolPtr<WidgetContext> &&_ctx) noexcept
	:container(_container), ctx(std::move(_ctx)),
	 widget(container.pool, _pool) {}

inline void
WidgetContainerParser::CancelWidget() noexcept
{
	assert(tag == Tag::WIDGET);
	assert(widget.widget != nullptr);

	widget.widget.reset();
	tag = Tag::IGNORE;
}

inline bool
WidgetContainerParser::OnStartElementInWidget(XmlParserTagType type,
					      std::string_view name) noexcept
{
	SkipPrefix(name, "c:"sv);

	if (name == "widget"sv) {
		if (type == XmlParserTagType::CLOSE)
			tag = Tag::WIDGET;
	} else if (name == "path-info"sv) {
		tag = Tag::WIDGET_PATH_INFO;
	} else if (name == "param"sv ||
		   name == "parameter"sv) {
		tag = Tag::WIDGET_PARAM;
		widget.param.name.Clear();
		widget.param.value.Clear();
	} else if (name == "header"sv) {
		tag = Tag::WIDGET_HEADER;
		widget.param.name.Clear();
		widget.param.value.Clear();
	} else if (name == "view"sv) {
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
	if (tag == Tag::SCRIPT &&
	    !StringIsEqualIgnoreCase(xml_tag.name, "script"sv))
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
	} else if (xml_tag.name == "c:widget"sv) {
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
	} else if (StringIsEqualIgnoreCase(xml_tag.name, "script"sv)) {
		tag = Tag::SCRIPT;
		return true;
	} else {
		return false;
	}
}

static void
parser_widget_attr_finished(Widget &widget,
			    std::string_view name, std::string_view value)
{
	if (name == "type"sv) {
		if (value.empty())
			throw std::runtime_error("empty widget class name");

		widget.SetClassName(value);
	} else if (name == "id"sv) {
		if (!value.empty())
			widget.SetId(value);
	} else if (name == "display"sv) {
		if (value == "inline"sv)
			widget.display = Widget::Display::INLINE;
		else if (value == "none"sv)
			widget.display = Widget::Display::NONE;
		else
			throw std::runtime_error("Invalid widget 'display' attribute");
	} else if (name == "session"sv) {
		if (value == "resource"sv)
			widget.session_scope = Widget::SessionScope::RESOURCE;
		else if (value == "site"sv)
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
			CancelWidget();
		}

		break;

	case Tag::WIDGET_PARAM:
	case Tag::WIDGET_HEADER:
		assert(widget.widget != nullptr);

		if (attr.name == "name"sv) {
			widget.param.name.Set(attr.value);
		} else if (attr.name == "value"sv) {
			widget.param.value.Set(attr.value);
		}

		break;

	case Tag::WIDGET_PATH_INFO:
		assert(widget.widget != nullptr);

		if (attr.name == "value"sv)
			widget.widget->from_template.path_info
				= p_strdup(widget.pool, attr.value);

		break;

	case Tag::WIDGET_VIEW:
		assert(widget.widget != nullptr);

		if (attr.name == "name"sv) {
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

[[gnu::pure]]
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

static bool
expansible_buffer_append_uri_escaped(ExpansibleBuffer &buffer,
				     std::string_view value) noexcept
{
	std::byte *w = buffer.BeginWrite(value.size() * 3);
	if (w == nullptr)
		return false;

	char *escaped = reinterpret_cast<char *>(w);
	size_t length = UriEscape(escaped, value);
	buffer.CommitWrite(length);
	return true;
}

bool
WidgetContainerParser::OnXmlTagFinished(const XmlParserTag &xml_tag) noexcept
{
	if (tag == Tag::WIDGET) {
		if (xml_tag.type == XmlParserTagType::OPEN || xml_tag.type == XmlParserTagType::SHORT) {
			if (!WantWidget(*widget.widget)) {
				CancelWidget();
				return true;
			}

			widget.start_offset = xml_tag.start;
		} else if (widget.widget == nullptr)
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
		if (value.find('&') != value.npos)
			value = unescape_dup(*tpool, html_escape_class, value);

		if (!widget.params.IsEmpty())
			widget.params.Write("&"sv);

		const auto name = widget.param.name.ReadStringView();
		expansible_buffer_append_uri_escaped(widget.params, name);

		widget.params.Write("="sv);

		expansible_buffer_append_uri_escaped(widget.params, value);
	} else if (tag == Tag::WIDGET_HEADER) {
		assert(widget.widget != nullptr);

		if (xml_tag.type == XmlParserTagType::CLOSE)
			return true;

		const auto name = widget.param.name.ReadStringView();
		if (!header_name_valid(name.data(), name.size())) {
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
