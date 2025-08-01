// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ResourceAddress.hxx"
#include "file/Address.hxx"
#include "http/local/Address.hxx"
#include "http/Address.hxx"
#include "http/Status.hxx"
#include "cgi/Address.hxx"
#include "uri/Extract.hxx"
#include "uri/Verify.hxx"
#include "uri/Base.hxx"
#include "uri/PNormalize.hxx"
#include "util/StringWithHash.hxx"
#include "AllocatorPtr.hxx"
#include "HttpMessageResponse.hxx"

#include <utility> // for std::unreachable()

using std::string_view_literals::operator""sv;

ResourceAddress::ResourceAddress(AllocatorPtr alloc,
				 const ResourceAddress &src) noexcept
{
	CopyFrom(alloc, src);
}

void
ResourceAddress::CopyFrom(AllocatorPtr alloc,
			  const ResourceAddress &src) noexcept
{
	type = src.type;

	switch (src.type) {
	case Type::NONE:
		break;

	case Type::LOCAL:
		assert(src.u.file != nullptr);
		u.file = alloc.New<FileAddress>(alloc, *src.u.file);
		break;

	case Type::HTTP:
		assert(src.u.http != nullptr);
		u.http = alloc.New<HttpAddress>(alloc, *src.u.http);
		break;

	case Type::LHTTP:
		assert(src.u.lhttp != nullptr);
		u.lhttp = src.u.lhttp->Dup(alloc);
		break;

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		u.cgi = src.u.cgi->Clone(alloc);
		break;
	}
}

ResourceAddress *
ResourceAddress::Dup(AllocatorPtr alloc) const noexcept
{
	return alloc.New<ResourceAddress>(alloc, *this);
}

ResourceAddress
ResourceAddress::WithPath(AllocatorPtr alloc, const char *path) const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		break;

	case Type::HTTP:
		return *alloc.New<HttpAddress>(ShallowCopy(), GetHttp(), path);

	case Type::LHTTP:
		return *alloc.New<LhttpAddress>(ShallowCopy(), GetLhttp(), path);
	}

	std::unreachable();
}

ResourceAddress
ResourceAddress::WithQueryStringFrom(AllocatorPtr alloc,
				     const char *uri) const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
		/* no query string support */
		return {ShallowCopy(), *this};

	case Type::HTTP:
		assert(u.http != nullptr);

		if (const char *query_string = UriQuery(uri); query_string != nullptr)
			return *u.http->InsertQueryString(alloc, query_string);
		else
			/* no query string in URI */
			return {ShallowCopy(), *this};

	case Type::LHTTP:
		assert(u.lhttp != nullptr);

		if (const char *query_string = UriQuery(uri); query_string != nullptr)
			return *u.lhttp->InsertQueryString(alloc, query_string);
		else
			/* no query string in URI */
			return {ShallowCopy(), *this};

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		assert(u.cgi->path != nullptr);

		if (const char *query_string = UriQuery(uri); query_string != nullptr) {
			auto *cgi = alloc.New<CgiAddress>(ShallowCopy(), GetCgi());
			cgi->InsertQueryString(alloc, query_string);
			return {type, *cgi};
		} else
			/* no query string in URI */
			return {ShallowCopy(), *this};
	}

	std::unreachable();
}

ResourceAddress
ResourceAddress::WithArgs(AllocatorPtr alloc,
			  std::string_view args, std::string_view path) const noexcept
{
	switch (type) {
		CgiAddress *cgi;

	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
		/* no arguments support */
		return {ShallowCopy(), *this};

	case Type::HTTP:
		assert(u.http != nullptr);

		return *GetHttp().InsertArgs(alloc, args, path);

	case Type::LHTTP:
		assert(u.lhttp != nullptr);

		return *GetLhttp().InsertArgs(alloc, args, path);

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		assert(u.cgi->path != nullptr);

		if (u.cgi->uri == nullptr && u.cgi->path_info == nullptr)
			return {ShallowCopy(), *this};

		cgi = alloc.New<CgiAddress>(ShallowCopy(), GetCgi());
		cgi->InsertArgs(alloc, args, path);
		return ResourceAddress(type, *cgi);
	}

	std::unreachable();
}

const char *
ResourceAddress::AutoBase(AllocatorPtr alloc, const char *uri) const noexcept
{
	assert(uri != nullptr);

	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
	case Type::HTTP:
	case Type::LHTTP:
		return nullptr;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->AutoBase(alloc, uri);
	}

	std::unreachable();
}

ResourceAddress
ResourceAddress::SaveBase(AllocatorPtr alloc,
			  std::string_view suffix) const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::PIPE:
		return nullptr;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		if (auto *cgi = GetCgi().SaveBase(alloc, suffix); cgi != nullptr)
			return {type, *cgi};
		else
			return nullptr;

	case Type::LOCAL:
		if (auto *file = GetFile().SaveBase(alloc, suffix); file != nullptr)
			return *file;
		else
			return nullptr;

	case Type::HTTP:
		if (auto *http = GetHttp().SaveBase(alloc, suffix); http != nullptr)
			return *http;
		else
			return nullptr;

	case Type::LHTTP:
		if (auto *lhttp = GetLhttp().SaveBase(alloc, suffix); lhttp != nullptr)
			return *lhttp;
		else
			return nullptr;
	}

	std::unreachable();
}

inline void
ResourceAddress::PostCacheStore(AllocatorPtr alloc) noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::PIPE:
		break;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		GetCgi().PostCacheStore(alloc);
		break;

	case Type::LOCAL:
	case Type::HTTP:
	case Type::LHTTP:
		break;
	}
}

void
ResourceAddress::CacheStore(AllocatorPtr alloc,
			    const ResourceAddress &src,
			    const char *uri, const char *base,
			    bool easy_base, bool expandable)
{
	if (base == nullptr) {
		CopyFrom(alloc, src);
		PostCacheStore(alloc);
		return;
	} else if (const char *tail = base_tail(uri, base)) {
		/* we received a valid BASE packet - store only the base
		   URI */

		if (easy_base || expandable) {
			/* when the response is expandable, skip appending the
			   tail URI, don't call SaveBase() */
			CopyFrom(alloc, src);
			PostCacheStore(alloc);
			return;
		}

		if (src.type == Type::NONE) {
			/* _save_base() will fail on a "NONE" address, but in this
			   case, the operation is useful and is allowed as a
			   special case */
			type = Type::NONE;
			return;
		}

		*this = src.SaveBase(alloc, tail);
		if (IsDefined()) {
			PostCacheStore(alloc);
			return;
		}

		/* the tail could not be applied to the address, so this is a
		   base mismatch */
	}

	throw HttpMessageResponse(HttpStatus::BAD_GATEWAY, "Base mismatch");
}

ResourceAddress
ResourceAddress::LoadBase(AllocatorPtr alloc,
			  std::string_view suffix) const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::PIPE:
		std::unreachable();

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		if (auto *cgi = GetCgi().LoadBase(alloc, suffix); cgi != nullptr)
			return {type, *cgi};
		else
			return nullptr;

	case Type::LOCAL:
		if (auto *file = GetFile().LoadBase(alloc, suffix); file != nullptr)
			return *file;
		else
			return nullptr;

	case Type::HTTP:
		if (auto *http = GetHttp().LoadBase(alloc, suffix); http != nullptr)
			return *http;
		else
			return nullptr;

	case Type::LHTTP:
		if (auto *lhttp = GetLhttp().LoadBase(alloc, suffix); lhttp != nullptr)
			return *lhttp;
		else
			return nullptr;
	}

	std::unreachable();
}

void
ResourceAddress::CacheLoad(AllocatorPtr alloc, const ResourceAddress &src,
			   const char *uri, const char *base,
			   bool unsafe_base, bool expandable)
{
	if (base != nullptr && !expandable) {
		const char *tail = require_base_tail(uri, base);

		/* strip leading slashes before normalizing the URI;
		   merging adjacent slashes is part of normalization,
		   but "tail" already comes after a slash */
		while (*tail == '/')
			++tail;

		tail = NormalizeUriPath(alloc, tail);

		if (!unsafe_base && !uri_path_verify_paranoid(tail))
			throw HttpMessageResponse(HttpStatus::BAD_REQUEST, "Malformed URI");

		if (src.type == Type::NONE) {
			/* see code comment in tcache_store_address() */
			type = Type::NONE;
			return;
		}

		*this = src.LoadBase(alloc, tail);
		if (IsDefined())
			return;
	}

	CopyFrom(alloc, src);
}

ResourceAddress
ResourceAddress::Apply(AllocatorPtr alloc,
		       std::string_view relative) const noexcept
{
	if (relative.empty())
		return {ShallowCopy(), *this};

	switch (type) {
	case Type::NONE:
		return nullptr;

	case Type::LOCAL:
	case Type::PIPE:
		return {ShallowCopy(), *this};

	case Type::HTTP:
		if (auto *http = u.http->Apply(alloc, relative); http != nullptr)
			return *http;
		else
			return nullptr;

	case Type::LHTTP:
		if (auto *lhttp = u.lhttp->Apply(alloc, relative); lhttp != nullptr)
			return *lhttp;
		else
			return nullptr;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		if (auto *cgi = u.cgi->Apply(alloc, relative); cgi != nullptr)
			return {type, *cgi};
		else
			return nullptr;

	}

	std::unreachable();
}

std::string_view
ResourceAddress::RelativeTo(const ResourceAddress &base) const noexcept
{
	assert(base.type == type);

	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
		return {};

	case Type::HTTP:
		return u.http->RelativeTo(*base.u.http);

	case Type::LHTTP:
		return u.lhttp->RelativeTo(*base.u.lhttp);

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->RelativeTo(*base.u.cgi);
	}

	std::unreachable();
}

std::string_view
ResourceAddress::RelativeToApplied(AllocatorPtr alloc,
				   const ResourceAddress &apply_base,
				   std::string_view relative) const
{
	assert(apply_base.type == type);

	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
	case Type::HTTP:
		break;

	case Type::LHTTP:
		return u.lhttp->RelativeToApplied(alloc, *apply_base.u.lhttp,
						  relative);

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->RelativeToApplied(alloc, *apply_base.u.cgi,
						relative);
	}

	auto applied = apply_base.Apply(alloc, relative);
	return applied.IsDefined()
		? applied.RelativeTo(*this)
		: std::string_view{};
}

StringWithHash
ResourceAddress::GetId(AllocatorPtr alloc) const noexcept
{
	switch (type) {
	case Type::NONE:
		return StringWithHash{""sv, 0};

	case Type::LOCAL:
		return StringWithHash{alloc.Dup(std::string_view{u.file->path})};

	case Type::HTTP:
		return StringWithHash{u.http->GetAbsoluteURI(alloc)};

	case Type::LHTTP:
		return u.lhttp->GetId(alloc);

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->GetId(alloc);
	}

	std::unreachable();
}

const char *
ResourceAddress::GetFilePath() const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::HTTP:
	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
	case Type::LHTTP:
		return nullptr;

	case Type::LOCAL:
		return u.file->path;
	}

	std::unreachable();
}

const char *
ResourceAddress::GetFileOrExecutablePath() const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::HTTP:
	case Type::PIPE:
		return nullptr;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->path;

	case Type::LHTTP:
		return u.lhttp->path;

	case Type::LOCAL:
		return u.file->path;
	}

	std::unreachable();
}

const char *
ResourceAddress::GetHostAndPort() const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return nullptr;

	case Type::HTTP:
		return u.http->host_and_port;

	case Type::LHTTP:
		return u.lhttp->host_and_port;
	}

	std::unreachable();
}

const char *
ResourceAddress::GetUriPath() const noexcept
{
	switch (type) {
	case Type::NONE:
	case Type::LOCAL:
	case Type::PIPE:
		return nullptr;

	case Type::HTTP:
		return u.http->path;

	case Type::LHTTP:
		return u.lhttp->uri;

	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		if (u.cgi->uri != nullptr)
			return u.cgi->uri;

		return u.cgi->script_name;
	}

	std::unreachable();
}

void
ResourceAddress::Check() const
{
	switch (type) {
	case Type::NONE:
		break;

	case Type::HTTP:
		u.http->Check();
		break;

	case Type::LOCAL:
		u.file->Check();
		break;

	case Type::LHTTP:
		u.lhttp->Check();
		break;

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		u.cgi->Check(type == Type::WAS);
		break;
	}
}

bool
ResourceAddress::IsValidBase() const noexcept
{
	switch (type) {
	case Type::NONE:
		return true;

	case Type::LOCAL:
		return u.file->IsValidBase();

	case Type::HTTP:
		return u.http->IsValidBase();

	case Type::LHTTP:
		return u.lhttp->IsValidBase();

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->IsValidBase();
	}

	std::unreachable();
}

bool
ResourceAddress::HasQueryString() const noexcept
{
	switch (type) {
	case Type::NONE:
		return false;

	case Type::LOCAL:
		return u.file->HasQueryString();

	case Type::HTTP:
		return u.http->HasQueryString();

	case Type::LHTTP:
		return u.lhttp->HasQueryString();

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->HasQueryString();
	}

	/* unreachable */
	assert(false);
	return false;
}

bool
ResourceAddress::IsExpandable() const noexcept
{
	switch (type) {
	case Type::NONE:
		return false;

	case Type::LOCAL:
		return u.file->IsExpandable();

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		return u.cgi->IsExpandable();

	case Type::HTTP:
		return u.http->IsExpandable();

	case Type::LHTTP:
		return u.lhttp->IsExpandable();
	}

	std::unreachable();
}

void
ResourceAddress::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	switch (type) {
	case Type::NONE:
		break;

	case Type::LOCAL:
		u.file->Expand(alloc, match_data);
		break;

	case Type::PIPE:
	case Type::CGI:
	case Type::FASTCGI:
	case Type::WAS:
		u.cgi->Expand(alloc, match_data);
		break;

	case Type::HTTP:
		u.http->Expand(alloc, match_data);
		break;

	case Type::LHTTP:
		u.lhttp->Expand(alloc, match_data);
		break;
	}
}
