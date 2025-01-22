// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "cluster/StickyHash.hxx"

#include <cstdint>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
struct ResourceAddress;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;

/**
 * Container for various additional parameters passed to
 * ResourceLoader::SendRequest().  Having this in a separate struct
 * unclutters the #ResourceLoader interface and allows adding more
 * parameters easily.
 */
struct ResourceRequestParams {
	/**
	 * A portion of the session id that is used to select
	 * the worker; 0 means disable stickiness.
	 */
	sticky_hash_t sticky_hash;

	/**
	 * A HTTP status code for protocols which do have one,
	 * e.g. pipe filters.
	 */
	HttpStatus status;

	bool ignore_no_cache;

	bool eager_cache;

	bool auto_flush_cache;

	bool want_metrics;

	/**
	 * If this is set, then the caller has already invoked
	 * ResourceAddress::GetId() and the cache doesn't need to do
	 * it again.
	 */
	const char *address_id;

	/**
	 * A unique identifier for the request body; if not nullptr,
	 * it may be used to cache POST requests.
	 */
	const char *body_etag;

	/**
	 * An opaque tag string to be assigned to the cache
	 * item (if the response is going to be cached by the
	 * #ResourceLoader); may be nullptr.
	 */
	const char *cache_tag;

	/**
	 * The name of the site this request belongs to; may
	 * be nullptr.
	 */
	const char *site_name;
};

/**
 * Load resources specified by a resource_address.
 */
class ResourceLoader {
public:
	/**
	 * Requests a resource.
	 *
	 * @param address the address of the resource
	 * @param body the request body
	 */
	virtual void SendRequest(struct pool &pool,
				 const StopwatchPtr &parent_stopwatch,
				 const ResourceRequestParams &params,
				 HttpMethod method,
				 const ResourceAddress &address,
				 StringMap &&headers,
				 UnusedIstreamPtr body,
				 HttpResponseHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept = 0;
};
