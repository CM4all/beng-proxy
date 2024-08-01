// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/ListenStreamStock.hxx"

class CancellablePointer;
class SocketDescriptor;
class SpawnService;
class TranslationService;

class SpawnListenStreamStockHandler final : public ListenStreamStockHandler {
	TranslationService &translation_service;
	SpawnService &spawn_service;

	class Process;
	class Request;

public:
	SpawnListenStreamStockHandler(TranslationService &_translation_service,
				      SpawnService &_spawn_service) noexcept
		:translation_service(_translation_service),
		spawn_service(_spawn_service) {}

	// virtual methods from class ListenStreamStockHandler
	void OnListenStreamReady(std::string_view key,
				 const char *socket_path, SocketDescriptor socket,
				 ListenStreamReadyHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept override;
};
