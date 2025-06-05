// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "translation/ListenStreamStockHandler.hxx"
#include "access_log/ChildErrorLogOptions.hxx"

struct BpInstance;
namespace Net::Log { class Sink; }

class BpListenStreamStockHandler final : public TranslationListenStreamStockHandler {
	BpInstance &instance;

	Net::Log::Sink *const log_sink;

	const ChildErrorLogOptions log_options;

	class Process;
	class HttpListener;

public:
	BpListenStreamStockHandler(BpInstance &_instance,
				   Net::Log::Sink *_log_sink,
				   const ChildErrorLogOptions &_log_options) noexcept;

	// virtual methods from class TranslationListenStreamStockHandler
	void Handle(const char *socket_path,
		    SocketDescriptor socket,
		    UniquePoolPtr<TranslateResponse> response,
		    ListenStreamReadyHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};
