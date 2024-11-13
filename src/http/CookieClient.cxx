// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CookieClient.hxx"
#include "CookieJar.hxx"
#include "CookieString.hxx"
#include "CommonHeaders.hxx"
#include "Quote.hxx"
#include "PTokenizer.hxx"
#include "strmap.hxx"
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/NumberParser.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <iterator>
#include <memory>
#include <string_view>

#include <string.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static bool
domain_matches(const char *domain, const char *match) noexcept
{
	size_t domain_length = strlen(domain);
	size_t match_length = strlen(match);

	return domain_length >= match_length &&
		strcasecmp(domain + domain_length - match_length, match) == 0 &&
		(domain_length == match_length || /* "a.b" matches "a.b" */
		 match[0] == '.' || /* "a.b" matches ".b" */
		 /* "a.b" matches "b" (implicit dot according to RFC 2965
		    3.2.2): */
		 (domain_length > match_length &&
		  domain[domain_length - match_length - 1] == '.'));
}

[[gnu::pure]]
static bool
path_matches(const char *path, const char *match) noexcept
{
	return match == nullptr || StringStartsWith(path, match);
}

template<typename L>
static void
cookie_list_delete_match(L &list,
			 const char *domain, const char *path,
			 std::string_view name) noexcept
{
	assert(domain != nullptr);

	list.remove_and_dispose_if([=](const Cookie &cookie){
		return domain_matches(domain, cookie.domain.c_str()) &&
			(cookie.path == nullptr
			 ? path == nullptr
			 : path_matches(cookie.path.c_str(), path)) &&
			name == cookie.name;
	},
		DeleteDisposer{});
}

static std::unique_ptr<Cookie>
parse_next_cookie(struct pool &tpool,
		  std::string_view &input) noexcept
{
	auto [name, value] = cookie_next_name_value(input, false);
	if (name.empty())
		return nullptr;

	auto cookie = std::make_unique<Cookie>(name, value);

	input = StripLeft(input);
	while (!input.empty() && input.front() == ';') {
		input = input.substr(1);

		const auto nv = http_next_name_value(tpool, input);
		name = nv.first;
		value = nv.second;
		if (StringIsEqualIgnoreCase(name, "domain"sv))
			cookie->domain = value;
		else if (StringIsEqualIgnoreCase(name, "path"sv))
			cookie->path = value;
		else if (StringIsEqualIgnoreCase(name, "max-age"sv)) {
			if (auto seconds = ParseInteger<unsigned>(value)) {
				if (*seconds == 0)
					cookie->expires = Expiry::AlreadyExpired();
				else
					cookie->expires.Touch(std::chrono::seconds(*seconds));
			}
		}

		input = StripLeft(input);
	}

	return cookie;
}

static bool
apply_next_cookie(CookieJar &jar, struct pool &tpool, std::string_view &input,
		  const char *domain, const char *path) noexcept
{
	assert(domain != nullptr);

	auto cookie = parse_next_cookie(tpool, input);
	if (cookie == nullptr)
		return false;

	if (cookie->domain == nullptr) {
		cookie->domain = domain;
	} else if (!domain_matches(domain, cookie->domain.c_str())) {
		/* discard if domain mismatch */
		return false;
	}

	if (path != nullptr && cookie->path != nullptr &&
	    !path_matches(path, cookie->path.c_str())) {
		/* discard if path mismatch */
		return false;
	}

	/* delete the old cookie */
	cookie_list_delete_match(jar.cookies, cookie->domain.c_str(),
				 cookie->path.c_str(),
				 (std::string_view)cookie->name);

	/* add the new one */

	if (!cookie->value.empty() && cookie->expires != Expiry::AlreadyExpired())
		jar.Add(*cookie.release());

	return true;
}

void
cookie_jar_set_cookie2(CookieJar &jar, const char *value,
		       const char *domain, const char *path) noexcept
{
	const TempPoolLease tpool;

	std::string_view input = value;
	while (1) {
		if (!apply_next_cookie(jar, tpool, input, domain, path))
			break;

		if (input.empty())
			return;

		if (input.front() != ',')
			break;

		input = StripLeft(input.substr(1));
	}
}

const char *
cookie_jar_http_header_value(const CookieJar &jar,
			     const char *domain, const char *path,
			     AllocatorPtr alloc) noexcept
{
	static constexpr size_t buffer_size = 4096;

	assert(domain != nullptr);
	assert(path != nullptr);

	if (jar.cookies.empty())
		return nullptr;

	const TempPoolLease tpool;

	char *buffer = (char *)p_malloc(tpool, buffer_size);

	size_t length = 0;

	for (auto i = jar.cookies.begin(), end = jar.cookies.end(), next = i;
	     i != end; i = next) {
		next = std::next(i);

		auto *const cookie = &*i;

		if (!domain_matches(domain, cookie->domain.c_str()) ||
		    !path_matches(path, cookie->path.c_str()))
			continue;

		const std::string_view name{cookie->name};
		const std::string_view value{cookie->value};

		if (buffer_size - length < name.size() + 1 + 1 + value.size() * 2 + 1 + 2)
			break;

		if (length > 0) {
			buffer[length++] = ';';
			buffer[length++] = ' ';
		}

		memcpy(buffer + length, name.data(), name.size());
		length += name.size();
		buffer[length++] = '=';
		if (http_must_quote_token(value))
			length += http_quote_string(buffer + length, value);
		else {
			memcpy(buffer + length, value.data(), value.size());
			length += value.size();
		}
	}

	const char *value;
	if (length > 0)
		value = alloc.DupZ({buffer, length});
	else
		value = nullptr;

	return value;
}

void
cookie_jar_http_header(const CookieJar &jar,
		       const char *domain, const char *path,
		       StringMap &headers, AllocatorPtr alloc) noexcept
{
	const char *cookie =
		cookie_jar_http_header_value(jar, domain, path, alloc);

	if (cookie != nullptr) {
		headers.Add(alloc, cookie2_header, "$Version=\"1\"");
		headers.Add(alloc, cookie_header, cookie);
	}
}
