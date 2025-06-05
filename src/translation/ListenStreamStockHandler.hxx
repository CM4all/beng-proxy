// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "net/ListenStreamStock.hxx"

template<typename T> class UniquePoolPtr;
struct TranslateResponse;
class TranslationService;

class TranslationListenStreamStockHandler : public ListenStreamStockHandler {
	TranslationService &translation_service;

	class Request;

public:
	explicit TranslationListenStreamStockHandler(TranslationService &_translation_service) noexcept
		:translation_service(_translation_service) {}

	// virtual methods from class ListenStreamStockHandler
	void OnListenStreamReady(std::string_view key,
				 const char *socket_path, SocketDescriptor socket,
				 ListenStreamReadyHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept final;

protected:
	virtual void Handle(const char *socket_path,
			    SocketDescriptor socket,
			    UniquePoolPtr<TranslateResponse> response,
			    ListenStreamReadyHandler &handler,
			    CancellablePointer &cancel_ptr) = 0;
};
