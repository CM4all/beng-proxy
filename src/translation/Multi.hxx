// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Service.hxx"

#include <list>
#include <memory>

/**
 * Wrapper for multiple #TranslationService.  This class implements
 * #TranslationCommand::DEFER.
 */
class MultiTranslationService final : public TranslationService {
	using List = std::list<std::shared_ptr<TranslationService>>;

	List items;

	class Request;

public:
	MultiTranslationService() = default;

	template<typename T>
	explicit MultiTranslationService(T &&t) noexcept {
		for (auto &i : t)
			Add(i);
	}

	template<typename T>
	void Add(T &&service) noexcept {
		items.emplace_back(std::forward<T>(service));
	}

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
