/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "cluster/AddressList.hxx"

#include <string_view>

struct StringView;
class AllocatorPtr;
class MatchData;

/**
 * The address of a resource stored on a HTTP server.
 */
struct HttpAddress {
	const bool ssl;

	bool http2 = false;

	/**
	 * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
	 * translation cache.
	 */
	bool expand_path = false;

	/**
	 * The name of the SSL/TLS client certificate to be used.
	 */
	const char *certificate = nullptr;

	/**
	 * The host part of the URI (including the port, if any).  nullptr if
	 * this is HTTP over UNIX domain socket.
	 */
	const char *host_and_port;

	/**
	 * The path component of the URI, starting with a slash.
	 */
	const char *path;

	AddressList addresses;

	HttpAddress(bool _ssl,
		    const char *_host_and_port, const char *_path);

	HttpAddress(ShallowCopy, bool _ssl,
		    const char *_host_and_port, const char *_path,
		    const AddressList &_addresses);

	constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src)
		:ssl(src.ssl), http2(src.http2),
		 expand_path(src.expand_path),
		 certificate(src.certificate),
		 host_and_port(src.host_and_port),
		 path(src.path),
		 addresses(shallow_copy, src.addresses)
	{
	}

	constexpr HttpAddress(HttpAddress &&src):HttpAddress(ShallowCopy(), src) {}

	HttpAddress(AllocatorPtr alloc, const HttpAddress &src);
	HttpAddress(AllocatorPtr alloc, const HttpAddress &src, const char *_path);

	constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src,
			      const char *_path)
		:ssl(src.ssl), http2(src.http2),
		 certificate(src.certificate),
		 host_and_port(src.host_and_port),
		 path(_path),
		 addresses(shallow_copy, src.addresses)
	{
	}

	HttpAddress &operator=(const HttpAddress &) = delete;

	/**
	 * Check if this instance is relative to the base, and return the
	 * relative part.  Returns nullptr if both URIs do not match.
	 */
	[[gnu::pure]]
	StringView RelativeTo(const HttpAddress &base) const;

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const;

	/**
	 * Build the absolute URI from this object, but use the specified path
	 * instead.
	 */
	[[gnu::malloc]]
	char *GetAbsoluteURI(AllocatorPtr alloc,
			     const char *override_path) const noexcept;

	/**
	 * Build the absolute URI from this object.
	 */
	[[gnu::malloc]]
	char *GetAbsoluteURI(AllocatorPtr alloc) const noexcept;

	[[gnu::pure]]
	bool HasQueryString() const;

	/**
	 * Duplicates this #http_address object and inserts the specified
	 * query string into the URI.
	 */
	[[gnu::malloc]]
	HttpAddress *InsertQueryString(AllocatorPtr alloc,
				       const char *query_string) const;

	/**
	 * Duplicates this #http_address object and inserts the specified
	 * arguments into the URI.
	 */
	[[gnu::malloc]]
	HttpAddress *InsertArgs(AllocatorPtr alloc,
				StringView args, StringView path_info) const;

	[[gnu::pure]]
	bool IsValidBase() const;

	[[gnu::malloc]]
	HttpAddress *SaveBase(AllocatorPtr alloc,
			      std::string_view suffix) const noexcept;

	[[gnu::malloc]]
	HttpAddress *LoadBase(AllocatorPtr alloc,
			      std::string_view suffix) const noexcept;

	const HttpAddress *Apply(AllocatorPtr alloc, StringView relative) const;

	/**
	 * Does this address need to be expanded with http_address_expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const {
		return expand_path;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);

	[[gnu::pure]]
	int GetDefaultPort() const {
		return ssl ? 443 : 80;
	}
};

/**
 * Parse the given absolute URI into a newly allocated
 * #http_address object.
 *
 * Throws std::runtime_error on error.
 */
[[gnu::malloc]]
HttpAddress *
http_address_parse(AllocatorPtr alloc, const char *uri);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The string pointers are stored,
 * they are not duplicated.
 */
[[gnu::malloc]]
HttpAddress *
http_address_with_path(AllocatorPtr alloc,
		       const HttpAddress *uwa,
		       const char *path);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The strings from the source object
 * are duplicated, but the "path" parameter is not.
 */
[[gnu::malloc]]
HttpAddress *
http_address_dup_with_path(AllocatorPtr alloc,
			   const HttpAddress *uwa,
			   const char *path);
