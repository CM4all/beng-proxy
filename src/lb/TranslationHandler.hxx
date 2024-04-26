// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Goto.hxx"
#include "translation/Stock.hxx"
#include "util/StringLess.hxx"

#include <map>
#include <memory>

struct CacheStats;
struct LbTranslationHandlerConfig;
class LbGotoMap;
struct IncomingHttpRequest;
struct TranslationInvalidateRequest;
struct TranslateResponse;
class EventLoop;
class CancellablePointer;
class LbTranslationCache;

class LbTranslationHandler final {
	const char *const name;

	TranslationStock stock;

	const std::map<const char *, LbGoto, StringLess> destinations;

	std::unique_ptr<LbTranslationCache> cache;

public:
	LbTranslationHandler(EventLoop &event_loop, LbGotoMap &goto_map,
			     const LbTranslationHandlerConfig &_config);
	~LbTranslationHandler() noexcept;

	[[gnu::pure]]
	CacheStats GetCacheStats() const noexcept;

	void FlushCache();
	void InvalidateCache(const TranslationInvalidateRequest &request);

	const LbGoto *FindDestination(const char *destination_name) const {
		auto i = destinations.find(destination_name);
		return i != destinations.end()
			? &i->second
			: nullptr;
	}

	void Pick(struct pool &pool, const IncomingHttpRequest &request,
		  const char *listener_tag,
		  TranslateHandler &handler,
		  CancellablePointer &cancel_ptr);

	void PutCache(const IncomingHttpRequest &request,
		      const char *listener_tag,
		      const TranslateResponse &response);
};
