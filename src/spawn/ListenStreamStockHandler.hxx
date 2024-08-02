// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "translation/ListenStreamStockHandler.hxx"

class SpawnService;

class SpawnListenStreamStockHandler final : public TranslationListenStreamStockHandler {
	SpawnService &spawn_service;

	class Process;

public:
	SpawnListenStreamStockHandler(TranslationService &_translation_service,
				      SpawnService &_spawn_service) noexcept
		:TranslationListenStreamStockHandler(_translation_service),
		 spawn_service(_spawn_service) {}

	// virtual methods from class TranslationListenStreamStockHandler
	DisposablePointer Handle(const char *socket_path,
				 SocketDescriptor socket,
				 const TranslateResponse &response,
				 ListenStreamReadyHandler &handler) override;
};
