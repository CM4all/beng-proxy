// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/ShallowCopy.hxx"

#include <cassert>
#include <cstddef>
#include <string_view>

struct StringWithHash;
struct FileAddress;
struct LhttpAddress;
struct HttpAddress;
struct CgiAddress;
class MatchData;
class AllocatorPtr;

/**
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 */
struct ResourceAddress {
	enum class Type {
		NONE,
		LOCAL,
		HTTP,
		LHTTP,
		PIPE,
		CGI,
		FASTCGI,
		WAS,
	};

	Type type;

private:
	union U {
		FileAddress *file;

		HttpAddress *http;

		LhttpAddress *lhttp;

		 CgiAddress *cgi;

		U() = default;
		constexpr U(std::nullptr_t n) noexcept:file(n) {}
		constexpr U(FileAddress &_file) noexcept:file(&_file) {}
		constexpr U(HttpAddress &_http) noexcept:http(&_http) {}
		constexpr U(LhttpAddress &_lhttp) noexcept:lhttp(&_lhttp) {}
		constexpr U(CgiAddress &_cgi) noexcept:cgi(&_cgi) {}
	} u;

public:
	ResourceAddress() = default;

	constexpr ResourceAddress(std::nullptr_t n) noexcept
		:type(Type::NONE), u(n) {}

	constexpr ResourceAddress(FileAddress &file) noexcept
		:type(Type::LOCAL), u(file) {}

	constexpr ResourceAddress(HttpAddress &http) noexcept
		:type(Type::HTTP), u(http) {}

	constexpr ResourceAddress(LhttpAddress &lhttp) noexcept
		:type(Type::LHTTP), u(lhttp) {}

	constexpr ResourceAddress(Type _type,
				  CgiAddress &cgi) noexcept
		:type(_type), u(cgi) {}

	constexpr ResourceAddress(ShallowCopy, const ResourceAddress &src) noexcept
		:type(src.type), u(src.u) {}

	constexpr ResourceAddress(ResourceAddress &&src) noexcept
		:ResourceAddress(ShallowCopy(), src) {}

	ResourceAddress(AllocatorPtr alloc, const ResourceAddress &src) noexcept;

	ResourceAddress &operator=(ResourceAddress &&) = default;

	constexpr bool IsDefined() const noexcept {
		return type != Type::NONE;
	}

	void Clear() noexcept {
		type = Type::NONE;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const;

	bool IsHttp() const noexcept {
		return type == Type::HTTP;
	}

	[[gnu::pure]]
	bool IsAnyHttp() const noexcept {
		return IsHttp() || type == Type::LHTTP;
	}

	/**
	 * Is this a CGI address, or a similar protocol?
	 */
	bool IsCgiAlike() const noexcept {
		return type == Type::CGI || type == Type::FASTCGI || type == Type::WAS;
	}

	[[gnu::pure]]
	const FileAddress &GetFile() const noexcept {
		assert(type == Type::LOCAL);

		return *u.file;
	}

	[[gnu::pure]]
	FileAddress &GetFile() noexcept {
		assert(type == Type::LOCAL);

		return *const_cast<FileAddress *>(u.file);
	}

	[[gnu::pure]]
	const HttpAddress &GetHttp() const noexcept {
		assert(type == Type::HTTP);

		return *u.http;
	}

	[[gnu::pure]]
	HttpAddress &GetHttp() noexcept {
		assert(type == Type::HTTP);

		return *const_cast<HttpAddress *>(u.http);
	}

	[[gnu::pure]]
	const LhttpAddress &GetLhttp() const noexcept {
		assert(type == Type::LHTTP);

		return *u.lhttp;
	}

	[[gnu::pure]]
	LhttpAddress &GetLhttp() noexcept {
		assert(type == Type::LHTTP);

		return *const_cast<LhttpAddress *>(u.lhttp);
	}

	[[gnu::pure]]
	const CgiAddress &GetCgi() const noexcept {
		assert(IsCgiAlike() || type == Type::PIPE);

		return *u.cgi;
	}

	[[gnu::pure]]
	CgiAddress &GetCgi() noexcept {
		assert(IsCgiAlike() || type == Type::PIPE);

		return *const_cast<CgiAddress *>(u.cgi);
	}

	[[gnu::pure]]
	bool HasQueryString() const noexcept;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	/**
	 * @return the path of the (data) file, or nullptr if this
	 * address contains none
	 */
	[[gnu::pure]]
	const char *GetFilePath() const noexcept;

	/**
	 * @return the path of the data file or the executable, or
	 * nullptr if this address contains none
	 */
	[[gnu::pure]]
	const char *GetFileOrExecutablePath() const noexcept;

	/**
	 * Determine the URI path.  May return nullptr if unknown or not
	 * applicable.
	 */
	[[gnu::pure]]
	const char *GetHostAndPort() const noexcept;

	/**
	 * Determine the URI path.  May return nullptr if unknown or not
	 * applicable.
	 */
	[[gnu::pure]]
	const char *GetUriPath() const noexcept;

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.
	 */
	[[gnu::pure]]
	StringWithHash GetId(AllocatorPtr alloc) const noexcept;

	void CopyFrom(AllocatorPtr alloc, const ResourceAddress &src) noexcept;

	[[gnu::malloc]]
	ResourceAddress *Dup(AllocatorPtr alloc) const noexcept;

	/**
	 * Construct a copy of this object with a different HTTP URI
	 * path component.
	 *
	 * This is a shallow copy: no memory is duplicated; the new
	 * instance contains pointers to the this instance and to the
	 * given path parameter.
	 */
	ResourceAddress WithPath(AllocatorPtr alloc,
				 const char *path) const noexcept;

	/**
	 * Construct a copy of this object and insert the query string
	 * from the specified URI.  If this resource address does not
	 * support a query string, or if the URI does not have one, the
	 * unmodified original #ResourceAddress is returned.
	 *
	 * This is a shallow copy: no memory is duplicated; the new
	 * instance contains pointers to the this instance and to the
	 * given path parameter.
	 */
	ResourceAddress WithQueryStringFrom(AllocatorPtr alloc,
					    const char *uri) const noexcept;

	/**
	 * Construct a copy of this object and insert the URI
	 * arguments and the path suffix.  If this resource address does not
	 * support the operation, the original #ResourceAddress pointer may
	 * be returned.
	 *
	 * This is a shallow copy: no memory is duplicated; the new
	 * instance contains pointers to the this instance and to the
	 * given path parameter.
	 */
	ResourceAddress WithArgs(AllocatorPtr alloc,
				 std::string_view args, std::string_view path) const noexcept;

	/**
	 * Check if a "base" URI can be generated automatically from this
	 * #ResourceAddress.  This applies when the CGI's PATH_INFO matches
	 * the end of the specified URI.
	 *
	 * @param uri the request URI
	 * @return a newly allocated base, or nullptr if that is not possible
	 */
	[[gnu::malloc]]
	const char *AutoBase(AllocatorPtr alloc, const char *uri) const noexcept;

	/**
	 * Duplicate a resource address, but return the base address.
	 *
	 * @param suffix the suffix to be removed from #src
	 * @return nullptr if the suffix does not match, or if this address type
	 * cannot have a base address
	 */
	[[gnu::pure]]
	ResourceAddress SaveBase(AllocatorPtr alloc,
				 std::string_view suffix) const noexcept;

	/**
	 * Duplicate a resource address, and append a suffix.
	 *
	 * Warning: this function does not check for excessive "../"
	 * sub-strings.
	 *
	 * @param suffix the suffix to be addded to #src
	 * @return nullptr if this address type cannot have a base address
	 */
	[[gnu::pure]]
	ResourceAddress LoadBase(AllocatorPtr alloc,
				 std::string_view suffix) const noexcept;

	/**
	 * Copies data from #src for storing in the translation cache.
	 *
	 * Throws HttpMessageResponse(HttpStatus::BAD_REQUEST) on base
	 * mismatch.
	 */
	void CacheStore(AllocatorPtr alloc, const ResourceAddress &src,
			const char *uri, const char *base,
			bool easy_base, bool expandable);

	/**
	 * Load an address from a cached object, and apply any BASE
	 * changes (if a BASE is present).
	 *
	 * Throws std::runtime_error on error.
	 */
	void CacheLoad(AllocatorPtr alloc, const ResourceAddress &src,
		       const char *uri, const char *base,
		       bool unsafe_base, bool expandable);

	[[gnu::pure]]
	ResourceAddress Apply(AllocatorPtr alloc,
			      std::string_view relative) const noexcept;

	[[gnu::pure]]
	std::string_view RelativeTo(const ResourceAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	[[gnu::pure]]
	std::string_view RelativeToApplied(AllocatorPtr alloc,
					   const ResourceAddress &apply_base,
					   std::string_view relative) const;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept;

	/**
	 * Expand the expand_path_info attribute.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);
};
