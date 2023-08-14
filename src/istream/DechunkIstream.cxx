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
#include "util/StaticVector.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

class DechunkIstream final : public FacadeIstream, DestructAnchor {
	/* DeferEvent is used to defer an
	   DechunkHandler::OnDechunkEnd() call */

	HttpChunkParser parser;

	bool had_input, had_output;

	/**
	 * The amount of raw (chunked) input which has been put into
	 * #chunks already.
	 */
	std::size_t parsed_input = 0;

	struct ParsedChunk {
		std::size_t header = 0, data = 0;
	};

	StaticVector<ParsedChunk, 8> chunks;

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

	bool AddHeader(std::size_t size) noexcept {
		assert(size > 0);

		if (!chunks.empty() && chunks.back().data == 0) {
			chunks.back().header += size;
			return true;
		} else if (!chunks.full()) {
			chunks.emplace_back().header = size;
			return true;
		} else
			return false;
	}

	bool AddData(std::size_t size) noexcept {
		assert(size > 0);

		if (!chunks.empty()) {
			chunks.back().data += size;
			return true;
		} else if (!chunks.full()) {
			chunks.emplace_back().data = size;
			return true;
		} else
			return false;
	}

	/**
	 * Parse the chunk boundaries from the raw (chunked) input and
	 * update #chunkls and #parsed_input.
	 *
	 * Throws on error.
	 */
	void ParseInput(std::span<const std::byte> src);

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

void
DechunkIstream::ParseInput(std::span<const std::byte> src)
{
	if (parser.HasEnded())
		/* don't accept any more data after the EOF chunk */
		return;

	while (!src.empty()) {
		const auto data = parser.Parse(src);

		if (data.begin() > src.begin()) {
			const std::size_t size = std::distance(src.begin(), data.begin());
			if (!AddHeader(size))
				return;

			parsed_input += size;
		}

		if (!data.empty()) {
			if (!AddData(data.size()))
				return;

			parsed_input += data.size();
			parser.Consume(data.size());
		}

		src = {data.end(), src.end()};

		if (parser.HasEnded()) {
			dechunk_handler.OnDechunkEndSeen();
			break;
		}
	}
}


/*
 * istream handler
 *
 */

size_t
DechunkIstream::OnData(std::span<const std::byte> src) noexcept
{
	const DestructObserver destructed{*this};

	const auto begin = src.begin();

	/* doing this in a loop because ParseInput() may be incomplete
	   because the "chunks" array gets full */
	while (!src.empty() && (!chunks.empty() || !parser.HasEnded())) {
		/* parse chunk boundaries */

		if (src.size() > parsed_input && !parser.HasEnded()) {
			try {
				ParseInput(src.subspan(parsed_input));
			} catch (...) {
				DestroyError(std::current_exception());
				return 0;
			}
		}

		/* submit all data chunks to our handler */

		while (!chunks.empty()) {
			auto &chunk = chunks.front();
			assert(chunk.header > 0 || chunk.data > 0);

			/* skip the header */

			if (src.size() < chunk.header) {
				/* there's not enough data to skip the
				   whole header */
				chunk.header -= src.size();
				parsed_input -= src.size();
				src = src.subspan(src.size());
				break;
			}

			parsed_input -= chunk.header;
			src = src.subspan(chunk.header);
			chunk.header = 0;

			/* handle data */

			const std::size_t data_size = std::min(src.size(), chunk.data);
			if (data_size > 0) {
				std::size_t n = InvokeData(src.first(data_size));
				if (n == 0 && destructed)
					return 0;

				parsed_input -= n;
				src = src.subspan(n);
				chunk.data -= n;

				if (n < data_size)
					/* not everything was
					   consumed: stop here */
					return std::distance(begin, src.begin());
			}

			if (chunk.data > 0)
				/* there was not enough data */
				break;

			chunks.pop_front();
		}
	}

	if (chunks.empty() && parser.HasEnded() && !EofDetected())
		return 0;

	return std::distance(begin, src.begin());
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
	if (!partial && !parser.HasEnded())
		return -1;

	std::size_t total = 0;

	for (const auto &chunk : chunks) {
		assert(chunk.header > 0 || chunk.data > 0);
		total += chunk.data;
	}

	return total;
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
