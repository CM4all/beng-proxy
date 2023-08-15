// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DechunkIstream.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "http/ChunkParser.hxx"
#include "event/DeferEvent.hxx"
#include "util/DestructObserver.hxx"
#include "util/StaticVector.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

class IstreamBucketReader {
	IstreamBucketList::const_iterator i;
	const IstreamBucketList::const_iterator end;

	std::size_t position = 0;

public:
	explicit IstreamBucketReader(const IstreamBucketList &list) noexcept
		:i(list.begin()), end(list.end()) {}

	std::size_t Skip(std::size_t size) noexcept {
		std::size_t result = 0;
		while (size > 0 && i != end && i->IsBuffer()) {
			const auto b = i->GetBuffer().subspan(position);
			assert(!b.empty());
			if (b.size() <= size) {
				result += b.size();
				++i;
				position = 0;
			} else {
				result += size;
				position += size;
				break;
			}
		}

		return result;
	}

	std::span<const std::byte> ReadSome(std::size_t size) noexcept {
		assert(size > 0);

		if (i == end || !i->IsBuffer())
			return {};

		const auto b = i->GetBuffer().subspan(position);
		assert(!b.empty());
		if (b.size() <= size) {
			++i;
			position = 0;
			return b;
		} else {
			position += size;
			return b.first(size);
		}
	}
};

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

	/**
	 * The amount of input that needs to be submitted to
	 * AddHeader().  This variable is necessary if #chunks is full
	 * and a new header cannot be added there.
	 */
	std::size_t pending_header = 0;

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

	DechunkHandler::DechunkInputAction InvokeDechunkEnd() noexcept;

	/**
	 * @return false if the input has been closed
	 */
	DechunkHandler::DechunkInputAction EofDetected() noexcept;

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
	 *
	 * @return true if more data can be parsed
	 */
	bool ParseInput(std::span<const std::byte> src);

public:
	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;

protected:
	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override {
		return InvokeReady();
	}

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

DechunkHandler::DechunkInputAction
DechunkIstream::InvokeDechunkEnd() noexcept
{

	assert(input.IsDefined());
	assert(parser.HasEnded());

	const auto action = dechunk_handler.OnDechunkEnd();

	switch (action) {
	case DechunkHandler::DechunkInputAction::ABANDON:
	case DechunkHandler::DechunkInputAction::DESTROYED:
		ClearInput();
		break;

	case DechunkHandler::DechunkInputAction::CLOSE:
		CloseInput();
		break;
	}

	return action;
}

DechunkHandler::DechunkInputAction
DechunkIstream::EofDetected() noexcept
{
	defer_eof_event.Schedule();
	return InvokeDechunkEnd();
}

bool
DechunkIstream::ParseInput(std::span<const std::byte> src)
{
	if (parser.HasEnded())
		/* don't accept any more data after the EOF chunk */
		return false;

	while (!src.empty()) {
		const auto data = parser.Parse(src);

		if (parser.HasEnded())
			dechunk_handler.OnDechunkEndSeen();

		const std::size_t header_size = std::distance(src.begin(), data.begin());
		parsed_input += header_size;
		pending_header += header_size;
		if (pending_header > 0 && !AddHeader(pending_header))
			return false;

		pending_header = 0;

		if (!data.empty()) {
			if (!AddData(data.size()))
				return false;

			parsed_input += data.size();
			parser.Consume(data.size());
		}

		src = {data.end(), src.end()};

		if (parser.HasEnded())
			return false;
	}

	return true;
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
	bool again;
	do {
		again = false;

		/* apply pending_header */

		if (pending_header > 0 && AddHeader(pending_header))
			pending_header = 0;

		/* parse chunk boundaries */

		if (src.size() > parsed_input) {
			try {
				ParseInput(src.subspan(parsed_input));
			} catch (...) {
				DestroyError(std::current_exception());
				return 0;
			}

			again = !parser.HasEnded();
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
				again = false;
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

			if (chunk.data > 0) {
				/* there was not enough data */
				again = false;
				break;
			}

			chunks.pop_front();
			again = true;
		}
	} while (again);

	if (chunks.empty() && parser.HasEnded()) {
		switch (EofDetected()) {
		case DechunkHandler::DechunkInputAction::ABANDON:
			break;

		case DechunkHandler::DechunkInputAction::DESTROYED:
		case DechunkHandler::DechunkInputAction::CLOSE:
			return 0;
		}
	}

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

void
DechunkIstream::_FillBucketList(IstreamBucketList &list)
{
	if (IsEofPending())
		return;

	IstreamBucketList tmp;
	FillBucketListFromInput(tmp);

	std::size_t skip = parsed_input;
	for (const auto &i : tmp) {
		if (!i.IsBuffer()) {
			list.SetMore();
			break;
		}

		auto b = i.GetBuffer();
		if (b.size() <= skip) {
			skip -= b.size();
			continue;
		}

		b = b.subspan(skip);
		skip = 0;

		try {
			if (!ParseInput(b)) {
				if (!parser.HasEnded())
					/* there's more data, but our
					   "chunks" array does not
					   have enough room */
					list.SetMore();
				break;
			}
		} catch (...) {
			Destroy();
			throw;
		}
	}

	if (!parser.HasEnded()) {
		if (!tmp.HasMore() && !list.HasMore())
			throw std::runtime_error{"premature EOF in dechunker"};

		list.SetMore();
	}

	IstreamBucketReader r{tmp};

	for (const auto chunk : chunks) {
		assert(chunk.header > 0 || chunk.data > 0);

		const std::size_t skipped = r.Skip(chunk.header);
		if (skipped < chunk.header)
			break;

		std::size_t remaining = chunk.data;
		while (remaining > 0) {
			const auto data = r.ReadSome(remaining);
			if (data.empty())
				break;

			list.Push(data);
			remaining -= data.size();
		}
	}
}

Istream::ConsumeBucketResult
DechunkIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	if (IsEofPending())
		return {0, true};

	std::size_t headers = 0, consumed = 0;

	auto i = chunks.begin();

	for (const auto end = chunks.end(); i != end; ++i) {
		auto &chunk = *i;
		assert(chunk.header > 0 || chunk.data > 0);

		headers += chunk.header;
		chunk.header = 0;

		if (nbytes < chunk.data) {
			chunk.data -= nbytes;
			consumed += nbytes;
			break;
		}

		consumed += chunk.data;
		nbytes -= chunk.data;
	}

	chunks.erase(chunks.begin(), i);

	if (chunks.empty()) {
		headers += pending_header;
		pending_header = 0;
	}

	parsed_input -= headers + consumed;

	[[maybe_unused]]
	const auto r = input.ConsumeBucketList(headers + consumed);
	assert(r.consumed == headers + consumed);

	const bool at_eof = chunks.empty() && parser.HasEnded();
	if (at_eof)
		InvokeDechunkEnd();

	return {Consumed(consumed), at_eof};
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
