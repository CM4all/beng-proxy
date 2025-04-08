// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Widget.hxx"
#include "View.hxx"
#include "Class.hxx"
#include "pool/tpool.hxx"
#include "http/Address.hxx"
#include "http/local/Address.hxx"
#include "cgi/Address.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Args.hxx"
#include "uri/PEdit.hxx"
#include "uri/PRelative.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"
#include "util/StringSplit.hxx"

#include <assert.h>

using std::string_view_literals::operator""sv;

/**
 * Returns the "base" address of the widget, i.e. without the widget
 * parameters from the parent container.
 */
ResourceAddress
Widget::GetBaseAddress(AllocatorPtr alloc, bool stateful) const noexcept
{
	const ResourceAddress &src = stateful
		? GetAddress()
		: GetStatelessAddress();

	if (!src.IsHttp() || from_template.query_string == nullptr)
		return {ShallowCopy(), src};

	const auto &src_http = src.GetHttp();
	const char *const src_path = src_http.path;

	const char *uri = uri_delete_query_string(alloc, src_path,
						  from_template.query_string);

	if (!from_request.query_string.empty())
		uri = uri_delete_query_string(alloc, src_path,
					      from_request.query_string);

	if (uri == src_path)
		return {ShallowCopy(), src};

	return src.WithPath(alloc, uri);
}

[[gnu::pure]]
static const ResourceAddress &
widget_get_original_address(const Widget &widget) noexcept
{
	assert(widget.cls != nullptr);

	const WidgetView *view = widget.GetAddressView();
	assert(view != nullptr);

	return view->address;
}

[[gnu::pure]]
static bool
HasTrailingSlash(const char *p) noexcept
{
	size_t length = strlen(p);
	return length > 0 && p[length - 1] == '/';
}

const ResourceAddress &
Widget::DetermineAddress(bool stateful) const noexcept
{
	const AllocatorPtr alloc(pool);

	const char *path_info, *uri;
	ResourceAddress *address;

	assert(cls != nullptr);

	path_info = GetPathInfo(stateful);
	assert(path_info != nullptr);

	const auto &original_address = widget_get_original_address(*this);
	if ((!stateful || from_request.query_string.empty()) &&
	    *path_info == 0 &&
	    from_template.query_string == nullptr)
		return original_address;

	switch (original_address.type) {
		CgiAddress *cgi;

	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::LOCAL:
	case ResourceAddress::Type::PIPE:
		break;

	case ResourceAddress::Type::HTTP:
		assert(original_address.GetHttp().path != nullptr);

		uri = original_address.GetHttp().path;

		if (*path_info != 0) {
			if (*path_info == '/' && HasTrailingSlash(uri))
				/* avoid generating a double slash when concatenating
				   URI path and path_info */
				++path_info;

			uri = alloc.Concat(uri, path_info);
		}

		if (from_template.query_string != nullptr)
			uri = uri_insert_query_string(alloc, uri,
						      from_template.query_string);

		if (stateful && from_request.query_string.data() != nullptr)
			uri = uri_append_query_string_n(alloc, uri,
							from_request.query_string);

		return *alloc.New<ResourceAddress>(original_address.WithPath(alloc, uri));

	case ResourceAddress::Type::LHTTP:
		assert(original_address.GetLhttp().uri != nullptr);

		uri = original_address.GetLhttp().uri;

		if (*path_info != 0) {
			if (*path_info == '/' && HasTrailingSlash(uri))
				/* avoid generating a double slash when concatenating
				   URI path and path_info */
				++path_info;

			uri = alloc.Concat(uri, path_info);
		}

		if (from_template.query_string != nullptr)
			uri = uri_insert_query_string(alloc, uri,
						      from_template.query_string);

		if (stateful && from_request.query_string.data() != nullptr)
			uri = uri_append_query_string_n(alloc, uri,
							from_request.query_string);

		return *alloc.New<ResourceAddress>(original_address.WithPath(alloc, uri));

	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
		address = original_address.Dup(alloc);
		cgi = &address->GetCgi();

		if (*path_info != 0)
			cgi->path_info = cgi->path_info != nullptr
				? uri_absolute(alloc, cgi->path_info, path_info)
				: path_info;

		if (!stateful || from_request.query_string.empty())
			cgi->query_string = from_template.query_string;
		else if (from_template.query_string == nullptr)
			cgi->query_string = alloc.DupZ(from_request.query_string);
		else
			cgi->query_string =
				alloc.Concat(from_request.query_string,
					     '&',
					     from_template.query_string);

		return *address;
	}

	return original_address;
}

const char *
Widget::AbsoluteUri(AllocatorPtr alloc, bool stateful,
		    std::string_view relative_uri) const noexcept
{
	assert(GetAddress().IsHttp());

	if (SkipPrefix(relative_uri, "~/"sv)) {
		stateful = false;
	} else if (!relative_uri.empty() && relative_uri.front() == '/' &&
		   cls != nullptr && cls->anchor_absolute) {
		relative_uri = relative_uri.substr(1);
		stateful = false;
	}

	const auto *uwa =
		&(stateful
		  ? GetAddress()
		  : GetStatelessAddress()).GetHttp();
	const char *base = uwa->path;
	if (relative_uri.data() == nullptr)
		return uwa->GetAbsoluteURI(alloc);

	const char *uri = uri_absolute(alloc, base, relative_uri);
	assert(uri != nullptr);
	if (!relative_uri.empty() &&
	    from_template.query_string != nullptr)
		/* the relative_uri is non-empty, and uri_absolute() has
		   removed the query string: re-add the configured query
		   string */
		uri = uri_insert_query_string(alloc, uri,
					      from_template.query_string);

	return uwa->GetAbsoluteURI(alloc, uri);
}

std::string_view
Widget::RelativeUri(AllocatorPtr alloc, bool stateful,
		    std::string_view relative_uri) const noexcept
{
	const ResourceAddress *base;
	if (SkipPrefix(relative_uri, "~/"sv)) {
		base = &widget_get_original_address(*this);
	} else if (relative_uri.starts_with('/') &&
		   cls != nullptr && cls->anchor_absolute) {
		relative_uri = relative_uri.substr(1);
		base = &widget_get_original_address(*this);
	} else
		base = alloc.New<ResourceAddress>(GetBaseAddress(alloc, stateful));

	const auto &original_address = widget_get_original_address(*this);
	return original_address.RelativeToApplied(alloc, *base, relative_uri);
}

/**
 * Returns true when the widget has the specified widget path.
 *
 * @param other the path to compare with; may be nullptr (i.e. never
 * matches)
 */
[[gnu::pure]]
static bool
compare_widget_path(const Widget *widget, const char *other) noexcept
{
	assert(widget != nullptr);

	if (other == nullptr)
		return false;

	const char *path = widget->GetIdPath();
	if (path == nullptr)
		return false;

	return StringIsEqual(path, other);
}

const char *
Widget::ExternalUri(AllocatorPtr alloc,
		    std::string_view external_base_uri,
		    const StringMap *args,
		    bool stateful,
		    std::string_view relative_uri,
		    const char *frame, const char *view) const noexcept
{
	const char *path = GetIdPath();
	if (path == nullptr ||
	    external_base_uri.data() == nullptr ||
	    cls == &root_widget_class)
		return nullptr;

	const TempPoolLease tpool;

	std::string_view p{};
	if (relative_uri.data() != nullptr) {
		p = RelativeUri(*tpool, stateful, relative_uri);
		if (p.data() == nullptr)
			return nullptr;
	}

	if (p.data() != nullptr &&
	    relative_uri.find('?') == relative_uri.npos &&
	    from_template.query_string != nullptr) {
		/* no query string in relative_uri: if there is one in the new
		   URI, check it and remove the configured parameters */
		const char *uri =
			uri_delete_query_string(*tpool, p_strdup(*tpool, p),
						from_template.query_string);
		p = uri;
	}

	std::string_view query_string{};
	if (const auto qmark = p.find('?'); qmark != p.npos) {
		/* separate query_string from path_info */
		const auto s = Partition(p, qmark);
		p = s.first;
		query_string = s.second;
	}

	std::string_view suffix = ""sv;
	if (p.data() != nullptr && cls->direct_addressing &&
	    compare_widget_path(this, frame)) {
		/* new-style direct URI addressing: append */
		suffix = p;
		p = {};
	}

	/* the URI is relative to the widget's base URI.  Convert the URI
	   into an absolute URI to the template page on this server and
	   add the appropriate args. */
	const char *args2 =
		args_format_n(*tpool, args,
			      "focus", path,
			      p.data() == nullptr ? nullptr : "path", p,
			      frame == nullptr ? nullptr : "frame",
			      frame == nullptr ? std::string_view{} : std::string_view{frame},
			      nullptr);

	return alloc.Concat(external_base_uri, ';',
			    args2,
			    std::string_view{"&view=", size_t(view != nullptr ? 6 : 0)},
			    view != nullptr ? view : "",
			    std::string_view{"/", size_t(suffix.size() > 0)},
			    suffix, query_string);
}
