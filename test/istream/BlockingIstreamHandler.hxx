// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream/Handler.hxx"

#include <cassert>

class BlockingIstreamHandler : public IstreamHandler {
public:
	enum class State {
		OPEN,
		EOF_,
		ERROR
	} state = State::OPEN;

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte>) noexcept override {
		assert(state = State::OPEN);
		return 0;
	}

	void OnEof() noexcept override {
		assert(state = State::OPEN);
		state = State::EOF_;
	}

	void OnError(std::exception_ptr &&) noexcept override {
		assert(state = State::OPEN);
		state = State::ERROR;
	}
};
