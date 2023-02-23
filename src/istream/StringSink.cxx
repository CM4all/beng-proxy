// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StringSink.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"

class StringSink final : IstreamSink, Cancellable {
	std::string value;

	StringSinkHandler &handler;

public:
	StringSink(UnusedIstreamPtr &&_input,
		   StringSinkHandler &_handler,
		   CancellablePointer &cancel_ptr)
		:IstreamSink(std::move(_input)),
		 handler(_handler)
	{
		cancel_ptr = *this;
	}

	void Read() noexcept {
		input.Read();
	}

private:
	void Destroy() {
		this->~StringSink();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(std::span<const std::byte> src) noexcept override {
		value.append(ToStringView(src));
		return src.size();
	}

	void OnEof() noexcept override {
		ClearInput();

		auto &_handler = handler;
		auto _value = std::move(value);
		Destroy();
		_handler.OnStringSinkSuccess(std::move(_value));
	}

	void OnError(std::exception_ptr ep) noexcept override {
		ClearInput();

		auto &_handler = handler;
		Destroy();
		_handler.OnStringSinkError(std::move(ep));
	}
};

/*
 * constructor
 *
 */

StringSink &
NewStringSink(struct pool &pool, UnusedIstreamPtr input,
	      StringSinkHandler &handler, CancellablePointer &cancel_ptr)
{
	return *NewFromPool<StringSink>(pool, std::move(input),
					handler, cancel_ptr);
}

void
ReadStringSink(StringSink &sink) noexcept
{
	sink.Read();
}
