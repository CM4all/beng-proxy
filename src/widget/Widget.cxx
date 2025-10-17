// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Widget.hxx"
#include "Class.hxx"
#include "View.hxx"
#include "Ref.hxx"
#include "Error.hxx"
#include "pool/tpool.hxx"
#include "util/HexFormat.hxx"
#include "util/Cast.hxx"
#include "util/StringAPI.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>

static constexpr bool
valid_prefix_start_char(char ch) noexcept
{
	return (ch >= 'A' && ch <= 'Z') ||
		(ch >= 'a' && ch <= 'z') ||
		ch == '_';
}

static constexpr bool
valid_prefix_char(char ch) noexcept
{
	return valid_prefix_start_char(ch) ||
		(ch >= '0' && ch <= '9');
}

static size_t
count_invalid_chars(const char *p)
{
	assert(*p != 0);

	size_t n = 0;
	if (!valid_prefix_start_char(*p))
		++n;

	for (++p; *p != 0; ++p)
		if (!valid_prefix_char(*p))
			++n;

	return n;
}

static constexpr char *
quote_byte(char *p, uint8_t ch) noexcept
{
	*p++ = '_';
	return HexFormatUint8Fixed(p, ch);
}

static const char *
quote_prefix(struct pool *pool, const char *p) noexcept
{
	if (*p == 0)
		return p;

	size_t n_quotes = count_invalid_chars(p);
	if (n_quotes == 0)
		/* no escaping needed */
		return p;

	const size_t src_length = strlen(p);
	char *buffer = (char *) p_malloc(pool, src_length + n_quotes * 2 + 1);
	char *q = buffer;

	if (!valid_prefix_start_char(*p))
		q = quote_byte(q, *p++);

	while (*p != 0) {
		if (!valid_prefix_char(*p))
			q = quote_byte(q, *p++);
		else
			*q++ = *p++;
	}

	*q = 0;
	return buffer;
}

void
Widget::SetId(const std::string_view _id) noexcept
{
	assert(parent != nullptr);
	assert(!_id.empty());

	const AllocatorPtr alloc(pool);

	id = alloc.DupZ(_id);

	const char *p = parent->GetIdPath();
	if (p != nullptr)
		id_path = *p == 0
			? id
			: alloc.Concat(p, WIDGET_REF_SEPARATOR, id);

	p = parent->GetPrefix();
	if (p != nullptr) {
		const TempPoolLease tpool;
		prefix = alloc.Concat(p, quote_prefix(tpool, id), "__");
	}
}

void
Widget::SetClassName(const std::string_view _class_name) noexcept
{
	assert(parent != nullptr);
	assert(class_name == nullptr);
	assert(cls == nullptr);

	const AllocatorPtr alloc(pool);

	class_name = alloc.DupZ(_class_name);
	quoted_class_name = quote_prefix(&pool, class_name);
}

const char *
Widget::GetLogName() const noexcept
{
	if (lazy.log_name != nullptr)
		return lazy.log_name;

	if (class_name == nullptr)
		return id;

	const AllocatorPtr alloc(pool);

	if (id_path == nullptr) {
		if (id != nullptr)
			return lazy.log_name = alloc.Concat(class_name,
							    "#(null)",
							    WIDGET_REF_SEPARATOR,
							    id);

		return class_name;
	}

	return lazy.log_name = alloc.Concat(class_name, '#', id_path);
}

std::string_view
Widget::LoggerDomain::GetDomain() const noexcept
{
	const auto &widget = ContainerCast(*this, (LoggerDomain Widget::*)&Widget::logger);
	return widget.GetLogName();
}

bool
Widget::IsContainerByDefault() const noexcept
{
	const WidgetView *v = GetDefaultView();
	return v != nullptr && v->IsContainer();
}

bool
Widget::HasProcessor() const noexcept
{
	const WidgetView *v = GetTransformationView();
	assert(v != nullptr);
	return v->HasProcessor();
}

bool
Widget::IsContainer() const noexcept
{
	const WidgetView *v = GetTransformationView();
	return v != nullptr && v->IsContainer();
}

Widget *
Widget::FindChild(std::string_view child_id) noexcept
{
	for (auto &child : children)
		if (child.id != nullptr && child_id == child.id)
			return &child;

	return nullptr;
}

void
Widget::CheckHost(const char *host, const char *site_name) const
{
	assert(cls != nullptr);

	try {
		cls->CheckHost(host, site_name);
	} catch (...) {
		std::throw_with_nested(WidgetError(*this,
						   WidgetErrorCode::FORBIDDEN,
						   "Untrusted host"));
	}
}

bool
widget_check_recursion(const Widget *widget) noexcept
{
	unsigned depth = 0;

	assert(widget != nullptr);

	do {
		if (++depth >= 8)
			return true;

		widget = widget->parent;
	} while (widget != nullptr);

	return false;
}

void
Widget::DiscardForFocused() noexcept
{
	/* the request body was not forwarded to the focused widget,
	   so discard it */
	if (for_focused != nullptr)
		DeleteFromPool(pool, std::exchange(for_focused, nullptr));
}

void
Widget::Cancel() noexcept
{
	/* we are not going to consume the request body, so abort it */
	from_request.body.Clear();

	DiscardForFocused();
}
