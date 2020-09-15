/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "TranslationCache.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/InvalidateParser.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "util/StringView.hxx"

LbTranslationCache::Vary::Vary(const TranslateResponse &response)
	:host(response.VaryContains(TranslationCommand::HOST)),
	 listener_tag(response.VaryContains(TranslationCommand::LISTENER_TAG)) {}

gcc_pure
static StringView
WithVary(const char *value, bool vary)
{
	if (!vary)
		return nullptr;

	if (value == nullptr)
		return "";

	return value;
}

gcc_pure
static size_t
CalculateKeyIteratorBufferSize(StringView host, StringView listener_tag)
{
	/* the ones are: underscore, separator, underscore, null terminator */
	return 1 + host.size + 1 + 1 + listener_tag.size + 1;
}

/**
 * A helper class which generates key permutations for lookup.
 */
class LbTranslationCacheKeyIterator {
	static constexpr unsigned HOST = 0x1;
	static constexpr unsigned LISTENER_TAG = 0x2;

	const StringView host, listener_tag;

	std::unique_ptr<char[]> buffer;

	unsigned last = 4;

public:
	LbTranslationCacheKeyIterator(LbTranslationCache::Vary vary,
				      const IncomingHttpRequest &request,
				      const char *_listener_tag)
		:host(vary.host
		      ? WithVary(request.headers.Get("host"), vary.host)
		      : nullptr),
		 listener_tag(WithVary(_listener_tag, vary.listener_tag)),
		 buffer(new char[CalculateKeyIteratorBufferSize(host, listener_tag)]) {}

	/**
	 * Generates the next key.  Call this until it returns nullptr.
	 */
	const char *NextKey() {
		if (last <= 0)
			return nullptr;

		last = NextIndex(last);
		assert(last < 4);
		return MakeKey(last);
	}

	/**
	 * Generates a key for storing into the cache.
	 */
	const char *FullKey() const {
		return MakeKey((!host.IsNull() * HOST) |
			       (!listener_tag.IsNull() * LISTENER_TAG));
	}

private:
	static constexpr bool HasHost(unsigned i) {
		return i & HOST;
	}

	static constexpr bool HasListenerTag(unsigned i) {
		return i & LISTENER_TAG;
	}

	bool IsInactive(int i) const {
		assert(i < 4);

		return (HasHost(i) && host.IsNull()) ||
			(HasListenerTag(i) && listener_tag.IsNull());
	}

	unsigned NextIndex(unsigned i) const {
		assert(i <= 4);

		for (--i; IsInactive(i); --i) {}
		return i;
	}

	const char *MakeKey(unsigned i) const {
		assert(i < 4);

		char *result = buffer.get(), *p = result;

		if (HasHost(i)) {
			/* the underscore is just here to make a difference
			   between "wildcard" (nothing) and "empty value"
			   (underscore) */
			*p++ = '_';
			p = (char *)mempcpy(p, host.data, host.size);
		}

		*p++ = '|';

		if (HasListenerTag(i)) {
			/* see above for the underscore explanation */
			*p++ = '_';
			p = (char *)mempcpy(p, listener_tag.data, listener_tag.size);
		}

		*p = 0;
		return result;
	}
};

LbTranslationCache::Item::Item(const TranslateResponse &response)
	:status(response.status),
	 https_only(response.https_only)
{
	if (response.redirect != nullptr)
		redirect = response.redirect;

	if (response.message != nullptr)
		message = response.message;

	if (response.pool != nullptr)
		pool = response.pool;

	if (response.canonical_host != nullptr)
		canonical_host = response.canonical_host;

	if (response.site != nullptr)
		site = response.site;
}

size_t
LbTranslationCache::GetAllocatedMemory() const noexcept
{
	size_t result = 0;

	cache.ForEach([&result](const std::string &key, const Item &item){
		result += key.length() + item.GetAllocatedMemory();
	});

	return result;
}

void
LbTranslationCache::Clear()
{
	cache.Clear();
	seen_vary.Clear();
}

static bool
KeyVaryMatch(StringView item, const char *request)
{
	if (request == nullptr)
		return true;

	size_t request_size = strlen(request);
	return item.size == 1 + request_size &&
		item.front() == '_' &&
		memcmp(item.data + 1, request, request_size) == 0;
}

/**
 * Match a cache key generated by
 * LbTranslationCacheKeyIterator::MakeKey() against VARY settings in a
 * #TranslateRequest (for #CONTROL_TCACHE_INVALIDATE).
 */
static bool
MatchKey(const char *key, const TranslateRequest &request)
{
	const char *separator = strchr(key, '|');
	assert(separator != nullptr);

	return KeyVaryMatch({key, separator}, request.host) &&
		KeyVaryMatch(separator + 1, request.listener_tag);
}

static bool
MatchInvalidate(const std::string &item, const char *vary)
{
	return vary == nullptr || item == vary;
}

static bool
MatchItem(const LbTranslationCache::Item &item,
	  const TranslationInvalidateRequest &request)
{
	return MatchInvalidate(item.site, request.site);
}

void
LbTranslationCache::Invalidate(const TranslationInvalidateRequest &request)
{
	if ((request.host != nullptr && !seen_vary.host) ||
	    (request.listener_tag != nullptr && !seen_vary.listener_tag))
		return;

	cache.RemoveIf([&request](const std::string &key, const Item &item){
		return MatchKey(key.c_str(), request) && MatchItem(item, request);
	});
}

const LbTranslationCache::Item *
LbTranslationCache::Get(const IncomingHttpRequest &request,
			const char *listener_tag)
{
	LbTranslationCacheKeyIterator ki(seen_vary, request, listener_tag);

	while (const char *key = ki.NextKey()) {
		const LbTranslationCache::Item *item = cache.Get(key);
		if (item != nullptr) {
			logger(4, "hit '", key, "'");
			return item;
		}
	}

	logger(5, "miss");
	return nullptr;
}

void
LbTranslationCache::Put(const IncomingHttpRequest &request,
			const char *listener_tag,
			const TranslateResponse &response)
{
	if (response.max_age == std::chrono::seconds::zero())
		/* not cacheable */
		return;

	const Vary vary(response);

	if (!vary && !cache.IsEmpty()) {
		logger(4, "VARY disappeared, clearing cache");
		Clear();
	}

	seen_vary |= vary;

	LbTranslationCacheKeyIterator ki(vary, request, listener_tag);
	const char *key = ki.FullKey();

	logger(4, "store '", key, "'");

	cache.PutOrReplace(key, Item(response));
}
