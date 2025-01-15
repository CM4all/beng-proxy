// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility> // for std::pair

enum class FcgiRecordType : uint8_t;

class FcgiFrameHandler {
public:
	enum class FrameResult {
		SKIP,
		CONTINUE,
		STOP,
		CLOSED,
	};

	virtual void OnFrameConsumed([[maybe_unused]] std::size_t nbytes) noexcept {}
	virtual FrameResult OnFrameHeader(FcgiRecordType type, uint_least16_t request_id) = 0;
	virtual std::pair<FrameResult, std::size_t> OnFramePayload(std::span<const std::byte> src) = 0;
	virtual FrameResult OnFrameEnd() = 0;
};

class FcgiParser final {
	std::size_t remaining = 0, skip = 0;

	bool in_frame = false;

public:
	enum class FeedResult {
		OK,
		BLOCKING,
		MORE,
		STOP,
		CLOSED,
	};

	FeedResult Feed(std::span<const std::byte> src, FcgiFrameHandler &handler);

	std::size_t GetRemaining() const noexcept {
		return remaining;
	}

	void SkipCurrent() noexcept {
		skip += remaining;
		remaining = 0;
	}
};
