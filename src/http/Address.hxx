// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "cluster/AddressList.hxx"

#include <string_view>

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
		    const char *_host_and_port, const char *_path) noexcept;

	HttpAddress(ShallowCopy, bool _ssl,
		    const char *_host_and_port, const char *_path,
		    const AddressList &_addresses) noexcept;

	constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src) noexcept
		:ssl(src.ssl), http2(src.http2),
		 expand_path(src.expand_path),
		 certificate(src.certificate),
		 host_and_port(src.host_and_port),
		 path(src.path),
		 addresses(shallow_copy, src.addresses)
	{
	}

	constexpr HttpAddress(HttpAddress &&src) noexcept
		:HttpAddress(ShallowCopy(), src) {}

	HttpAddress(AllocatorPtr alloc, const HttpAddress &src) noexcept;
	HttpAddress(AllocatorPtr alloc, const HttpAddress &src, const char *_path) noexcept;

	constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src,
			      const char *_path) noexcept
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
	std::string_view RelativeTo(const HttpAddress &base) const noexcept;

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
	bool HasQueryString() const noexcept;

	/**
	 * Duplicates this #http_address object and inserts the specified
	 * query string into the URI.
	 */
	[[gnu::malloc]]
	HttpAddress *InsertQueryString(AllocatorPtr alloc,
				       const char *query_string) const noexcept;

	/**
	 * Duplicates this #http_address object and inserts the specified
	 * arguments into the URI.
	 */
	[[gnu::malloc]]
	HttpAddress *InsertArgs(AllocatorPtr alloc,
				std::string_view args,
				std::string_view path_info) const noexcept;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	[[gnu::malloc]]
	HttpAddress *SaveBase(AllocatorPtr alloc,
			      std::string_view suffix) const noexcept;

	[[gnu::malloc]]
	HttpAddress *LoadBase(AllocatorPtr alloc,
			      std::string_view suffix) const noexcept;

	HttpAddress *Apply(AllocatorPtr alloc,
			   std::string_view relative) const noexcept;

	/**
	 * Does this address need to be expanded with http_address_expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return expand_path;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);

	constexpr int GetDefaultPort() const noexcept {
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
		       const char *path) noexcept;

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The strings from the source object
 * are duplicated, but the "path" parameter is not.
 */
[[gnu::malloc]]
HttpAddress *
http_address_dup_with_path(AllocatorPtr alloc,
			   const HttpAddress *uwa,
			   const char *path) noexcept;
