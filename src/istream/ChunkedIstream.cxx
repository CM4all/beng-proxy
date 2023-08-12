// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChunkedIstream.hxx"
#include "FacadeIstream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "util/ConstBuffer.hxx"
#include "util/DestructObserver.hxx"
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"

#include <algorithm>
#include <array>
#include <cassert>

#include <string.h>

using std::string_view_literals::operator""sv;

class ChunkedIstream final : public FacadeIstream, DestructAnchor {
	/**
	 * This flag is true while writing the buffer inside _Read().
	 * OnData() will check it, and refuse to accept more data from the
	 * input.  This avoids writing the buffer recursively.
	 */
	bool writing_buffer = false;

	std::array<char, 7> buffer;
	size_t buffer_sent = buffer.size();

	size_t missing_from_current_chunk = 0;

public:
	ChunkedIstream(struct pool &p, UnusedIstreamPtr &&_input) noexcept
		:FacadeIstream(p, std::move(_input)) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;

	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override {
		auto result = InvokeReady();
		if (result != IstreamReadyResult::CLOSED && !HasInput())
			/* our input has meanwhile been closed */
			result = IstreamReadyResult::CLOSED;
		return result;
	}

	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

private:
	bool IsBufferEmpty() const noexcept {
		assert(buffer_sent <= buffer.size());

		return buffer_sent == buffer.size();
	}

	/** set the buffer length and return a pointer to the first byte */
	char *SetBuffer(size_t length) noexcept {
		assert(IsBufferEmpty());
		assert(length <= buffer.size());

		buffer_sent = buffer.size() - length;
		return buffer.data() + buffer_sent;
	}

	/** append data to the buffer */
	void AppendToBuffer(std::string_view src) noexcept;

	void StartChunk(size_t length) noexcept;

	std::span<const char> ReadBuffer() noexcept {
		return std::span{buffer}.subspan(buffer_sent);
	}

	size_t ConsumeBuffer(size_t nbytes) noexcept {
		size_t size = ReadBuffer().size();
		if (size > nbytes)
			size = nbytes;

		if (size > 0) {
			buffer_sent += size;
			Consumed(size);
		}

		return size;
	}

	/**
	 * Returns true if the buffer is consumed.
	 */
	bool SendBuffer() noexcept;

	/**
	 * Wrapper for SendBuffer() that sets and clears the
	 * #writing_buffer flag.  This requires acquiring a pool reference
	 * to do that safely.
	 *
	 * @return true if the buffer is consumed.
	 */
	bool SendBuffer2() noexcept;

	size_t Feed(std::span<const std::byte> src) noexcept;
};

void
ChunkedIstream::AppendToBuffer(std::string_view src) noexcept
{
	assert(!src.empty());
	assert(src.size() <= buffer_sent);

	const auto old = ReadBuffer();

#ifndef NDEBUG
	/* simulate a buffer reset; if we don't do this, an assertion in
	   SetBuffer() fails (which is invalid for this special case) */
	buffer_sent = buffer.size();
#endif

	auto dest = SetBuffer(old.size() + src.size());
	dest = std::copy(old.begin(), old.end(), dest);
	std::copy(src.begin(), src.end(), dest);
}

void
ChunkedIstream::StartChunk(size_t length) noexcept
{
	assert(length > 0);
	assert(IsBufferEmpty());
	assert(missing_from_current_chunk == 0);

	if (length > 0x8000)
		/* maximum chunk size is 32kB for now */
		length = 0x8000;

	missing_from_current_chunk = length;

	char *p = (char *)SetBuffer(6);
	p = HexFormatUint16Fixed(p, (uint16_t)length);
	*p++ = '\r';
	*p++ = '\n';
}

bool
ChunkedIstream::SendBuffer() noexcept
{
	auto r = ReadBuffer();
	if (r.empty())
		return true;

	size_t nbytes = InvokeData(std::as_bytes(r));
	if (nbytes > 0)
		buffer_sent += nbytes;

	return nbytes == r.size();
}

bool
ChunkedIstream::SendBuffer2() noexcept
{
	const DestructObserver destructed(*this);

	assert(!writing_buffer);
	writing_buffer = true;

	const bool result = SendBuffer();
	if (!destructed)
		writing_buffer = false;
	return result;
}

inline size_t
ChunkedIstream::Feed(const std::span<const std::byte> src) noexcept
{
	const DestructObserver destructed(*this);

	size_t total = 0, rest, nbytes;

	assert(input.IsDefined());

	do {
		assert(!writing_buffer);

		if (IsBufferEmpty() && missing_from_current_chunk == 0)
			StartChunk(src.size() - total);

		if (!SendBuffer())
			return destructed ? 0 : total;

		assert(IsBufferEmpty());

		if (missing_from_current_chunk == 0) {
			/* we have just written the previous chunk trailer;
			   re-start this loop to start a new chunk */
			nbytes = rest = 0;
			continue;
		}

		rest = src.size() - total;
		if (rest > missing_from_current_chunk)
			rest = missing_from_current_chunk;

		nbytes = InvokeData({src.data() + total, rest});
		if (nbytes == 0)
			return destructed ? 0 : total;

		total += nbytes;

		missing_from_current_chunk -= nbytes;
		if (missing_from_current_chunk == 0) {
			/* a chunk ends with "\r\n" */
			char *p = SetBuffer(2);
			p[0] = '\r';
			p[1] = '\n';
		}
	} while ((!IsBufferEmpty() || total < src.size()) && nbytes == rest);

	return total;
}


/*
 * istream handler
 *
 */

size_t
ChunkedIstream::OnData(std::span<const std::byte> src) noexcept
{
	if (writing_buffer)
		/* this is a recursive call from _Read(): bail out */
		return 0;

	return Feed(src);
}

void
ChunkedIstream::OnEof() noexcept
{
	assert(input.IsDefined());
	assert(missing_from_current_chunk == 0);

	input.Clear();

	/* write EOF chunk (length 0) */

	AppendToBuffer("0\r\n\r\n"sv);

	/* flush the buffer */

	if (SendBuffer())
		DestroyEof();
}

void
ChunkedIstream::OnError(std::exception_ptr ep) noexcept
{
	assert(input.IsDefined());

	input.Clear();
	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

off_t
ChunkedIstream::_GetAvailable(bool partial) noexcept
{
	if (!partial)
		return -1;

	off_t result = ReadBuffer().size();

	if (input.IsDefined()) {
		if (off_t available = input.GetAvailable(true); available > 0) {
			result += available;

			if (available >= (off_t)missing_from_current_chunk)
				/* new chunk header */
				result += 6;
		}

		/* EOF chunk */
		result += 5;
	}

	return result;
}

void
ChunkedIstream::_Read() noexcept
{
	if (!SendBuffer2())
		return;

	if (!input.IsDefined()) {
		DestroyEof();
		return;
	}

	if (IsBufferEmpty() && missing_from_current_chunk == 0) {
		off_t available = input.GetAvailable(true);
		if (available > 0) {
			StartChunk(available);
			if (!SendBuffer2())
				return;
		}
	}

	input.Read();
}

void
ChunkedIstream::_FillBucketList(IstreamBucketList &list)
{
	if (!input.IsDefined()) {
		// TODO: generate EOF chunk
		list.SetMore();
		return;
	}

	auto b = ReadBuffer();
	if (b.empty() && missing_from_current_chunk == 0) {
		off_t available = input.GetAvailable(true);
		if (available > 0) {
			StartChunk(available);
			b = ReadBuffer();
		}
	}

	if (!b.empty())
		list.Push(std::as_bytes(b));

	if (missing_from_current_chunk > 0) {
		assert(input.IsDefined());

		IstreamBucketList sub;
		FillBucketListFromInput(sub);

		size_t nbytes = list.SpliceBuffersFrom(std::move(sub),
						       missing_from_current_chunk);
		if (nbytes >= missing_from_current_chunk)
			list.Push(AsBytes(list.HasMore() ? "\r\n"sv : "\r\n0\r\n\r\n"sv));
	} else
		list.SetMore();
}

Istream::ConsumeBucketResult
ChunkedIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t total = 0;

	size_t size = ConsumeBuffer(nbytes);
	nbytes -= size;
	total += size;

	size = std::min(nbytes, missing_from_current_chunk);
	if (size > 0) {
		assert(input.IsDefined());

		const auto [consumed, is_eof] = input.ConsumeBucketList(size);
		if (is_eof)
			CloseInput();

		Consumed(consumed);
		nbytes -= consumed;
		total += consumed;

		missing_from_current_chunk -= consumed;
		if (missing_from_current_chunk == 0) {
			if (HasInput()) {
				/* a chunk ends with "\r\n" */
				char *p = SetBuffer(2);
				p[0] = '\r';
				p[1] = '\n';
			} else {
				const auto src = "\r\n0\r\n\r\n"sv;
				char *p = SetBuffer(src.size());
				std::copy(src.begin(), src.end(), p);
			}

			size = ConsumeBuffer(nbytes);
			nbytes -= size;
			total += size;
		}
	}

	return {total, missing_from_current_chunk == 0 && IsBufferEmpty() && !HasInput()};
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_chunked_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<ChunkedIstream>(pool, std::move(input));
}
