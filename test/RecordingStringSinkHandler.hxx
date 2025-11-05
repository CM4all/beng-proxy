// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/StringSink.hxx"
#include "util/Cancellable.hxx"

#include <cassert>
#include <variant>

class EventLoop;

class RecordingStringSinkHandler final : public StringSinkHandler {
	std::variant<std::monostate, std::string, std::exception_ptr> result;

public:
	CancellablePointer cancel_ptr;

	~RecordingStringSinkHandler() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	bool IsAlive() const noexcept {
		return std::holds_alternative<std::monostate>(result);
	}

	std::string TakeValue() && {
		assert(!std::holds_alternative<std::monostate>(result));

		if (std::holds_alternative<std::exception_ptr>(result))
			std::rethrow_exception(std::move(std::get<std::exception_ptr>(result)));

		return std::move(std::get<std::string>(result));
	}

	// virtual methods from StringSinkHandler

	void OnStringSinkSuccess(std::string &&value) noexcept override {
		assert(std::holds_alternative<std::monostate>(result));
		result = std::move(value);
		cancel_ptr = {};
	}

	void OnStringSinkError(std::exception_ptr error) noexcept override {
		assert(std::holds_alternative<std::monostate>(result));
		result = std::move(error);
		cancel_ptr = {};
	}
};
