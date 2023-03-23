// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "istream.hxx"

#include <utility>
#include <cstddef>
#include <cassert>

class UnusedIstreamPtr;

class IstreamPointer {
	Istream *stream = nullptr;

public:
	IstreamPointer() = default;

	IstreamPointer(UnusedIstreamPtr src,
		       IstreamHandler &handler) noexcept;

	IstreamPointer(IstreamPointer &&other) noexcept
		:stream(std::exchange(other.stream, nullptr)) {}

	IstreamPointer(const IstreamPointer &) = delete;
	IstreamPointer &operator=(const IstreamPointer &) = delete;

	bool IsDefined() const noexcept {
		return stream != nullptr;
	}

	void Clear() noexcept {
		stream = nullptr;
	}

	void Close() noexcept {
		assert(IsDefined());

		auto *old = std::exchange(stream, nullptr);
		old->Close();
	}

	UnusedIstreamPtr Steal() noexcept;

	void Set(UnusedIstreamPtr _stream,
		 IstreamHandler &handler) noexcept;

	void Set(Istream &_stream,
		 IstreamHandler &handler) noexcept {
		assert(!IsDefined());

		stream = &_stream;
		stream->SetHandler(handler);
	}

	template<typename I>
	void Replace(I &&_stream,
		     IstreamHandler &handler) noexcept {
		Close();
		Set(std::forward<I>(_stream), handler);
	}

	void SetDirect(FdTypeMask direct) noexcept {
		assert(IsDefined());

		stream->SetDirect(direct);
	}

	void Read() noexcept {
		assert(IsDefined());

		stream->Read();
	}

	void FillBucketList(IstreamBucketList &list) {
		assert(IsDefined());

		try {
			stream->FillBucketList(list);
		} catch (...) {
			/* if FillBucketList() fails, the Istream is destroyed, so
			   clear the pointer here */
			Clear();
			throw;
		}
	}

	auto ConsumeBucketList(std::size_t nbytes) noexcept {
		assert(IsDefined());

		return stream->ConsumeBucketList(nbytes);
	}

	void ConsumeDirect(std::size_t nbytes) noexcept {
		assert(IsDefined());

		stream->ConsumeDirect(nbytes);
	}

	[[gnu::pure]]
	off_t GetAvailable(bool partial) const noexcept {
		assert(IsDefined());

		return stream->GetAvailable(partial);
	}

	off_t Skip(off_t length) noexcept {
		assert(IsDefined());

		return stream->Skip(length);
	}

	int AsFd() noexcept {
		assert(IsDefined());

		int fd = stream->AsFd();
		if (fd >= 0)
			Clear();

		return fd;
	}
};
