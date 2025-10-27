// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ChunkedIstream.hxx"
#include "FacadeIstream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "util/DestructObserver.hxx"
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"

#include <algorithm>
#include <array>
#include <cassert>

#include <string.h>

using std::string_view_literals::operator""sv;

class ChunkedIstream final : public FacadeIstream, DestructAnchor {
	static constexpr std::size_t CHUNK_START_SIZE = 6;
	static constexpr std::size_t CHUNK_END_SIZE = 2;
	static constexpr std::size_t EOF_SIZE = 5;

	template<std::size_t SIZE>
	class Buffer {
		std::array<char, SIZE> buffer;
		std::size_t position = buffer.size();

	public:
		/**
		 * Set the buffer length and return a pointer to the
		 * first character to be written.
		 */
		[[nodiscard]]
		constexpr char *Set(std::size_t size) noexcept {
			assert(empty());
			assert(size <= buffer.size());

			position = buffer.size() - size;
			return buffer.data() + position;
		}

		constexpr void Set(std::string_view src) noexcept {
			std::copy(src.begin(), src.end(), Set(src.size()));
		}

		[[nodiscard]]
		constexpr char *Append(std::size_t size) noexcept {
			assert(size <= position);

			const auto old = ReadChars();

#ifndef NDEBUG
			/* simulate a buffer reset; if we don't do this, an assertion
			   in Set() fails (which is invalid for this special case) */
			position = buffer.size();
#endif

			return std::copy(old.begin(), old.end(), Set(old.size() + size));
		}

		/**
		 * Append data to the buffer.
		 */
		constexpr void Append(std::string_view src) noexcept {
			std::copy(src.begin(), src.end(), Append(src.size()));
		}

		[[nodiscard]]
		constexpr bool empty() const noexcept {
			assert(position <= buffer.size());

			return position == buffer.size();
		}

		[[nodiscard]]
		constexpr std::span<const char> ReadChars() const noexcept {
			assert(position <= buffer.size());

			return std::span{buffer}.subspan(position);
		}

		[[nodiscard]]
		constexpr std::span<const std::byte> Read() const noexcept {
			return std::as_bytes(ReadChars());
		}

		constexpr void Consume(std::size_t nbytes) noexcept {
			assert(position <= buffer.size());
			assert(nbytes <= buffer.size());

			position += nbytes;

			assert(position <= buffer.size());
		}
	};

	/**
	 * This flag is true while writing the buffer inside _Read().
	 * OnData() will check it, and refuse to accept more data from the
	 * input.  This avoids writing the buffer recursively.
	 */
	bool writing_buffer = false;

	Buffer<7> buffer;

	size_t missing_from_current_chunk = 0;

public:
	ChunkedIstream(struct pool &p, UnusedIstreamPtr &&_input) noexcept
		:FacadeIstream(p, std::move(_input)) {}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override;
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
	void OnError(std::exception_ptr &&ep) noexcept override;

private:
	void StartChunk(size_t length) noexcept;

	size_t ConsumeBuffer(size_t nbytes) noexcept {
		size_t size = buffer.Read().size();
		if (size > nbytes)
			size = nbytes;

		if (size > 0) {
			buffer.Consume(size);
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
ChunkedIstream::StartChunk(size_t length) noexcept
{
	assert(length > 0);
	assert(buffer.empty());
	assert(missing_from_current_chunk == 0);

	if (length > 0x8000)
		/* maximum chunk size is 32kB for now */
		length = 0x8000;

	missing_from_current_chunk = length;

	char *p = buffer.Set(6);
	p = HexFormatUint16Fixed(p, (uint16_t)length);
	*p++ = '\r';
	*p++ = '\n';
}

bool
ChunkedIstream::SendBuffer() noexcept
{
	auto r = buffer.Read();
	if (r.empty())
		return true;

	size_t nbytes = InvokeData(r);
	if (nbytes > 0)
		buffer.Consume(nbytes);

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

		if (buffer.empty() && missing_from_current_chunk == 0)
			StartChunk(src.size() - total);

		if (!SendBuffer())
			return destructed ? 0 : total;

		assert(buffer.empty());

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
			buffer.Set("\r\n"sv);
		}
	} while ((!buffer.empty() || total < src.size()) && nbytes == rest);

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

	buffer.Append("0\r\n\r\n"sv);

	/* flush the buffer */

	if (SendBuffer())
		DestroyEof();
}

void
ChunkedIstream::OnError(std::exception_ptr &&ep) noexcept
{
	assert(input.IsDefined());

	input.Clear();
	DestroyError(std::move(ep));
}

/*
 * istream implementation
 *
 */

IstreamLength
ChunkedIstream:: _GetLength() noexcept
{
	IstreamLength result{
		.length = buffer.Read().size(),
		.exhaustive = true,
	};

	if (missing_from_current_chunk > 0)
		/* end of the current chunk */
		result.length += CHUNK_END_SIZE;

	if (input.IsDefined()) {
		const auto from_input = input.GetLength();
		result += from_input;

		if (std::cmp_greater(from_input.length, missing_from_current_chunk))
			/* new chunk header and end */
			result.length += CHUNK_START_SIZE + CHUNK_END_SIZE;

		/* EOF chunk */
		result.length += EOF_SIZE;
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

	if (buffer.empty() && missing_from_current_chunk == 0) {
		if (const auto available = input.GetLength().length;
		    available > 0) {
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
		if (const auto r = buffer.Read(); !r.empty())
			list.Push(r);

		return;
	}

	IstreamBucketList sub;
	FillBucketListFromInput(sub);

	if (sub.IsEmpty() && !sub.HasMore()) {
		CloseInput();

		/* write EOF chunk (length 0) */
		buffer.Append("0\r\n\r\n"sv);
	}

	auto b = buffer.Read();
	if (b.empty() && missing_from_current_chunk == 0 && HasInput()) {
		/* see which of FillBucketList() and GetAvailable()
		   returns more data and use that to start the new
		   chunk */

		auto available = input.GetLength().length;
		if (std::cmp_greater(sub.GetTotalBufferSize(), available))
			available = sub.GetTotalBufferSize();

		if (available > 0) {
			StartChunk(available);
			b = buffer.Read();
		}
	}

	if (!b.empty())
		list.Push(b);

	if (missing_from_current_chunk > 0) {
		assert(input.IsDefined());

		size_t nbytes = list.SpliceBuffersFrom(std::move(sub),
						       missing_from_current_chunk);
		if (nbytes >= missing_from_current_chunk)
			list.Push(AsBytes(list.HasMore() ? "\r\n"sv : "\r\n0\r\n\r\n"sv));
	} else if (sub.HasMore()) {
		list.CopyMoreFlagsFrom(sub);
	} else if (!sub.IsEmpty()) {
		/* no new chunk was generated yet because our buffer
		   has no room yet for the chunk header, but there
		   will be one */
		list.SetMore();
	}
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
				buffer.Set("\r\n"sv);
			} else {
				buffer.Set("\r\n0\r\n\r\n"sv);
			}

			size = ConsumeBuffer(nbytes);
			nbytes -= size;
			total += size;
		}
	}

	return {total, missing_from_current_chunk == 0 && buffer.empty() && !HasInput()};
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
