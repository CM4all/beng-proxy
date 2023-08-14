// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DechunkIstream.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "http/ChunkParser.hxx"
#include "event/DeferEvent.hxx"
#include "util/DestructObserver.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

class DechunkIstream final : public FacadeIstream, DestructAnchor {
	/* DeferEvent is used to defer an
	   DechunkHandler::OnDechunkEnd() call */

	HttpChunkParser parser;

	bool had_input, had_output;

	bool seen_eof = false;

	/**
	 * Number of data chunk bytes already seen, but not yet consumed
	 * by our #IstreamHandler.
	 */
	size_t seen_data = 0;

	/**
	 * This event is used to defer an DechunkHandler::OnDechunkEnd()
	 * call.
	 */
	DeferEvent defer_eof_event;

	DechunkHandler &dechunk_handler;

public:
	DechunkIstream(struct pool &p, UnusedIstreamPtr &&_input,
		       EventLoop &event_loop,
		       DechunkHandler &_dechunk_handler) noexcept
		:FacadeIstream(p, std::move(_input)),
		 defer_eof_event(event_loop, BIND_THIS_METHOD(DeferredEof)),
		 dechunk_handler(_dechunk_handler)
	{
	}

private:
	void Abort(std::exception_ptr ep) noexcept;

	[[gnu::pure]]
	bool IsEofPending() const noexcept {
		return defer_eof_event.IsPending();
	}

	void DeferredEof() noexcept;

	/**
	 * @return false if the input has been closed
	 */
	bool EofDetected() noexcept;

	bool CalculateRemainingDataSize(const std::byte *src, const std::byte *src_end) noexcept;

	size_t Feed(std::span<const std::byte> src) noexcept;

public:
	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;

protected:
	/* virtual methods from class IstreamHandler */
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

void
DechunkIstream::Abort(std::exception_ptr ep) noexcept
{
	assert(!parser.HasEnded());
	assert(input.IsDefined());
	assert(!IsEofPending());

	DestroyError(ep);
}

void
DechunkIstream::DeferredEof() noexcept
{
	assert(parser.HasEnded());
	assert(!input.IsDefined());

	DestroyEof();
}

bool
DechunkIstream::EofDetected() noexcept
{
	assert(input.IsDefined());
	assert(parser.HasEnded());

	defer_eof_event.Schedule();

	bool result = dechunk_handler.OnDechunkEnd();
	if (result)
		ClearInput();
	else
		/* this code path is only used by the unit test */
		CloseInput();

	return result;
}

inline bool
DechunkIstream::CalculateRemainingDataSize(const std::byte *src,
					   const std::byte *const src_end) noexcept
{
	assert(!IsEofPending());

	seen_data = 0;

	if (parser.HasEnded()) {
		if (!seen_eof) {
			seen_eof = true;
			dechunk_handler.OnDechunkEndSeen();
		}

		return true;
	}

	/* work with a copy of our HttpChunkParser */
	HttpChunkParser p(parser);

	while (src != src_end) {
		const std::span<const std::byte> src_remaining(src, src_end - src);

		std::span<const std::byte> data;

		try {
			data = p.Parse(src_remaining);
		} catch (...) {
			Abort(std::current_exception());
			return false;
		}

		if (data.empty()) {
			if (p.HasEnded() && !seen_eof) {
				seen_eof = true;
				dechunk_handler.OnDechunkEndSeen();
			}

			break;
		}

		seen_data += data.size();
		p.Consume(data.size());
		src = data.data() + data.size();
	}

	return true;
}

size_t
DechunkIstream::Feed(const std::span<const std::byte> _src) noexcept
{
	assert(input.IsDefined());
	assert(!IsEofPending());

	const DestructObserver destructed(*this);

	had_input = true;

	const auto src_begin = _src.data();
	const auto src_end = src_begin + _src.size();

	auto src = src_begin;

	while (src != src_end) {
		const std::span<const std::byte> src_remaining(src, src_end - src);

		std::span<const std::byte> data;

		try {
			data = parser.Parse(src_remaining);
		} catch (...) {
			Abort(std::current_exception());
			return 0;
		}

		assert(data.data() >= src);
		assert(data.data() <= src_end);
		assert(data.data() + data.size() <= src_end);

		src = data.data();

		if (!data.empty()) {
			assert(!parser.HasEnded());

			size_t nbytes;

			had_output = true;
			seen_data += data.size();
			nbytes = InvokeData({src, data.size()});
			assert(nbytes <= data.size());

			if (destructed)
				return 0;

			if (nbytes == 0)
				break;

			src += nbytes;

			bool finished = parser.Consume(nbytes);
			if (!finished)
				break;
		} else if (parser.HasEnded()) {
			break;
		} else {
			assert(src == src_end);
		}
	}

	const size_t position = src - src_begin;
	if (parser.HasEnded())
		return EofDetected()
			? position
			: 0;

	if (!CalculateRemainingDataSize(src, src_end))
		return 0;

	return position;
}


/*
 * istream handler
 *
 */

size_t
DechunkIstream::OnData(std::span<const std::byte> src) noexcept
{
	if (IsEofPending())
		/* don't accept any more data after the EOF chunk */
		return 0;

	return Feed(src);
}

void
DechunkIstream::OnEof() noexcept
{
	input.Clear();

	if (IsEofPending())
		/* let DeferEvent handle this */
		return;

	DestroyError(std::make_exception_ptr(std::runtime_error("premature EOF in dechunker")));
}

void
DechunkIstream::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();

	if (IsEofPending())
		/* let DeferEvent handle this */
		return;

	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

off_t
DechunkIstream::_GetAvailable(bool partial) noexcept
{
	if (IsEofPending())
		return 0;

	if (!partial && !seen_eof)
		return -1;

	return seen_data;
}

void
DechunkIstream::_Read() noexcept
{
	if (IsEofPending())
		return;

	const DestructObserver destructed(*this);

	had_output = false;

	do {
		had_input = false;
		input.Read();
	} while (!destructed && input.IsDefined() && had_input && !had_output &&
		 !IsEofPending());
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_dechunk_new(struct pool &pool, UnusedIstreamPtr input,
		    EventLoop &event_loop,
		    DechunkHandler &dechunk_handler) noexcept
{
	return NewIstreamPtr<DechunkIstream>(pool, std::move(input),
					     event_loop,
					     dechunk_handler);
}
