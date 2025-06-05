// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Service.hxx"
#include "Stock.hxx"

struct TranslateRequest;
class TranslateHandler;

class TranslationGlue final : public TranslationService {
	class Request;

	TranslationStock stock;

public:
	TranslationGlue(EventLoop &event_loop, SocketAddress _address, unsigned limit) noexcept
		:stock(event_loop, _address, limit) {}

	auto &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
