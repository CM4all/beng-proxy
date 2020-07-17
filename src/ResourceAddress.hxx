/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef BENG_PROXY_RESOURCE_ADDRESS_HXX
#define BENG_PROXY_RESOURCE_ADDRESS_HXX

#include "util/ShallowCopy.hxx"
#include "util/Compiler.h"

#include <cstddef>

#include <assert.h>

struct StringView;
struct FileAddress;
struct LhttpAddress;
struct HttpAddress;
struct CgiAddress;
struct NfsAddress;
class MatchInfo;
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
		NFS,
	};

	Type type;

private:
	union U {
		const FileAddress *file;

		const HttpAddress *http;

		const LhttpAddress *lhttp;

		const CgiAddress *cgi;

		const NfsAddress *nfs;

		U() = default;
		constexpr U(std::nullptr_t n) noexcept:file(n) {}
		constexpr U(const FileAddress &_file) noexcept:file(&_file) {}
		constexpr U(const HttpAddress &_http) noexcept:http(&_http) {}
		constexpr U(const LhttpAddress &_lhttp) noexcept:lhttp(&_lhttp) {}
		constexpr U(const CgiAddress &_cgi) noexcept:cgi(&_cgi) {}
		constexpr U(const NfsAddress &_nfs) noexcept:nfs(&_nfs) {}
	} u;

public:
	ResourceAddress() = default;

	constexpr ResourceAddress(std::nullptr_t n) noexcept
		:type(Type::NONE), u(n) {}

	constexpr ResourceAddress(const FileAddress &file) noexcept
		:type(Type::LOCAL), u(file) {}

	constexpr ResourceAddress(const HttpAddress &http) noexcept
		:type(Type::HTTP), u(http) {}

	constexpr ResourceAddress(const LhttpAddress &lhttp) noexcept
		:type(Type::LHTTP), u(lhttp) {}

	constexpr ResourceAddress(Type _type,
				  const CgiAddress &cgi) noexcept
		:type(_type), u(cgi) {}

	constexpr ResourceAddress(const NfsAddress &nfs) noexcept
		:type(Type::NFS), u(nfs) {}

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

	gcc_pure
	bool IsAnyHttp() const noexcept {
		return IsHttp() || type == Type::LHTTP;
	}

	/**
	 * Is this a CGI address, or a similar protocol?
	 */
	bool IsCgiAlike() const noexcept {
		return type == Type::CGI || type == Type::FASTCGI || type == Type::WAS;
	}

	gcc_pure
	const FileAddress &GetFile() const noexcept {
		assert(type == Type::LOCAL);

		return *u.file;
	}

	gcc_pure
	FileAddress &GetFile() noexcept {
		assert(type == Type::LOCAL);

		return *const_cast<FileAddress *>(u.file);
	}

	gcc_pure
	const HttpAddress &GetHttp() const noexcept {
		assert(type == Type::HTTP);

		return *u.http;
	}

	gcc_pure
	HttpAddress &GetHttp() noexcept {
		assert(type == Type::HTTP);

		return *const_cast<HttpAddress *>(u.http);
	}

	gcc_pure
	const LhttpAddress &GetLhttp() const noexcept {
		assert(type == Type::LHTTP);

		return *u.lhttp;
	}

	gcc_pure
	LhttpAddress &GetLhttp() noexcept {
		assert(type == Type::LHTTP);

		return *const_cast<LhttpAddress *>(u.lhttp);
	}

	gcc_pure
	const NfsAddress &GetNfs() const noexcept {
		assert(type == Type::NFS);

		return *u.nfs;
	}

	gcc_pure
	NfsAddress &GetNfs() noexcept {
		assert(type == Type::NFS);

		return *const_cast<NfsAddress *>(u.nfs);
	}

	gcc_pure
	const CgiAddress &GetCgi() const noexcept {
		assert(IsCgiAlike() || type == Type::PIPE);

		return *u.cgi;
	}

	gcc_pure
	CgiAddress &GetCgi() noexcept {
		assert(IsCgiAlike() || type == Type::PIPE);

		return *const_cast<CgiAddress *>(u.cgi);
	}

	gcc_pure
	bool HasQueryString() const noexcept;

	gcc_pure
	bool IsValidBase() const noexcept;

	/**
	 * Determine the URI path.  May return nullptr if unknown or not
	 * applicable.
	 */
	gcc_pure
	const char *GetHostAndPort() const noexcept;

	/**
	 * Determine the URI path.  May return nullptr if unknown or not
	 * applicable.
	 */
	gcc_pure
	const char *GetUriPath() const noexcept;

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.
	 */
	gcc_pure
	const char *GetId(AllocatorPtr alloc) const noexcept;

	void CopyFrom(AllocatorPtr alloc, const ResourceAddress &src) noexcept;

	gcc_malloc
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
				 StringView args, StringView path) const noexcept;

	/**
	 * Check if a "base" URI can be generated automatically from this
	 * #ResourceAddress.  This applies when the CGI's PATH_INFO matches
	 * the end of the specified URI.
	 *
	 * @param uri the request URI
	 * @return a newly allocated base, or nullptr if that is not possible
	 */
	gcc_malloc
	const char *AutoBase(AllocatorPtr alloc, const char *uri) const noexcept;

	/**
	 * Duplicate a resource address, but return the base address.
	 *
	 * @param suffix the suffix to be removed from #src
	 * @return nullptr if the suffix does not match, or if this address type
	 * cannot have a base address
	 */
	gcc_pure
	ResourceAddress SaveBase(AllocatorPtr alloc,
				 const char *suffix) const noexcept;

	/**
	 * Duplicate a resource address, and append a suffix.
	 *
	 * Warning: this function does not check for excessive "../"
	 * sub-strings.
	 *
	 * @param suffix the suffix to be addded to #src
	 * @return nullptr if this address type cannot have a base address
	 */
	gcc_pure
	ResourceAddress LoadBase(AllocatorPtr alloc,
				 const char *suffix) const noexcept;

	/**
	 * Copies data from #src for storing in the translation cache.
	 *
	 * Throws HttpMessageResponse(HTTP_STATUS_BAD_REQUEST) on base
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

	gcc_pure
	ResourceAddress Apply(AllocatorPtr alloc, StringView relative) const noexcept;

	gcc_pure
	StringView RelativeTo(const ResourceAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	gcc_pure
	StringView RelativeToApplied(AllocatorPtr alloc,
				     const ResourceAddress &apply_base,
				     StringView relative) const;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	gcc_pure
	bool IsExpandable() const noexcept;

	/**
	 * Expand the expand_path_info attribute.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};

#endif
