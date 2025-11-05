// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StringSink.hxx"
#include "Bucket.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"

#include <cassert>

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
		switch (OnIstreamReady()) {
		case IstreamReadyResult::OK:
			break;

		case IstreamReadyResult::FALLBACK:
			input.Read();
			break;

		case IstreamReadyResult::CLOSED:
			break;
		}
	}

private:
	void Destroy() noexcept {
		this->~StringSink();
	}

	void DestroyEof() noexcept {
		auto &_handler = handler;
		auto _value = std::move(value);
		Destroy();
		_handler.OnStringSinkSuccess(std::move(_value));
	}

	void DestroyError(std::exception_ptr &&error) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.OnStringSinkError(std::move(error));
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override;

	size_t OnData(std::span<const std::byte> src) noexcept override {
		value.append(ToStringView(src));
		return src.size();
	}

	void OnEof() noexcept override {
		ClearInput();
		DestroyEof();
	}

	void OnError(std::exception_ptr &&ep) noexcept override {
		ClearInput();
		DestroyError(std::move(ep));
	}
};

IstreamReadyResult
StringSink::OnIstreamReady() noexcept
{
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		DestroyError(std::current_exception());
		return IstreamReadyResult::CLOSED;
	}

	auto more = list.GetMore();

	std::size_t nbytes = 0;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			more = IstreamBucketList::More::FALLBACK;
			break;
		}

		value.append(ToStringView(i.GetBuffer()));
		nbytes += i.GetBuffer().size();
	}

	if (nbytes > 0) {
		const auto result = input.ConsumeBucketList(nbytes);
		assert(result.consumed == nbytes);

		if (result.eof)
			more = IstreamBucketList::More::NO;
	}

	switch (more) {
	case IstreamBucketList::More::NO:
		DestroyEof();
		return IstreamReadyResult::CLOSED;

	case IstreamBucketList::More::PUSH:
	case IstreamBucketList::More::PULL:
		return IstreamReadyResult::OK;

	case IstreamBucketList::More::AGAIN:
		// TODO loop?
		return IstreamReadyResult::OK;

	case IstreamBucketList::More::FALLBACK:
		return IstreamReadyResult::FALLBACK;
	}

	std::unreachable();
}

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
