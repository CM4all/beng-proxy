// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "memory/GrowingBuffer.hxx"

#include <span>
#include <string_view>
#include <utility>

#include <stdint.h>

enum class TranslationCommand : uint16_t;
struct TranslateRequest;
class SocketAddress;

class TranslationMarshaller {
	GrowingBuffer buffer;

public:
	void Write(TranslationCommand command,
		   std::span<const std::byte> payload={});

	template<typename T>
	void Write(TranslationCommand command,
		   std::span<const T> payload) {
		Write(command, std::as_bytes(payload));
	}

	void Write(TranslationCommand command,
		   std::string_view payload);

	template<typename T>
	void WriteOptional(TranslationCommand command,
			   std::span<const T> payload) {
		if (payload.data() != nullptr)
			Write(command, payload);
	}

	void WriteOptional(TranslationCommand command,
			   const char *payload) {
		if (payload != nullptr)
			Write(command, payload);
	}

	template<typename T>
	void WriteT(TranslationCommand command, const T &payload) {
		Write(command, std::span{&payload, 1});
	}

	void Write16(TranslationCommand command, uint16_t payload) {
		WriteT<uint16_t>(command, payload);
	}

	void Write(TranslationCommand command,
		   TranslationCommand command_string,
		   SocketAddress address);

	void WriteOptional(TranslationCommand command,
			   TranslationCommand command_string,
			   SocketAddress address);

	GrowingBuffer Commit() {
		return std::move(buffer);
	}
};

GrowingBuffer
MarshalTranslateRequest(uint8_t PROTOCOL_VERSION,
			const TranslateRequest &request);
