// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "translation/ListenStreamStockHandler.hxx"

struct BpInstance;

class BpListenStreamStockHandler final : public TranslationListenStreamStockHandler {
	BpInstance &instance;

	class Process;
	class HttpListener;

public:
	explicit BpListenStreamStockHandler(BpInstance &instance) noexcept;

	// virtual methods from class TranslationListenStreamStockHandler
	DisposablePointer Handle(const char *socket_path,
				 SocketDescriptor socket,
				 const TranslateResponse &response,
				 ListenStreamReadyHandler &handler) override;
};
