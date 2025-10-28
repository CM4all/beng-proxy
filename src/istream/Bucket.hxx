// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/StaticVector.hxx"

#include <cassert>
#include <span>

struct iovec;

class IstreamBucket {
public:
	enum class Type {
		BUFFER,
	};

private:
	/* this definition exists so we can eventually implement
	   non-buffer buckets */
	static constexpr Type type = Type::BUFFER;

	union {
		std::span<const std::byte> buffer;
	};

public:
	explicit constexpr IstreamBucket(std::span<const std::byte> _buffer) noexcept
		:buffer(_buffer) {}


	constexpr Type GetType() const noexcept {
		return type;
	}

	constexpr bool IsBuffer() const noexcept {
		return type == Type::BUFFER;
	}

	constexpr std::span<const std::byte> GetBuffer() const noexcept {
		assert(type == Type::BUFFER);

		return buffer;
	}
};

class IstreamBucketList {
	static constexpr std::size_t CAPACITY = 64;
	using List = StaticVector<IstreamBucket, CAPACITY>;
	List list;

public:
	enum class More {
		/**
		 * This is all, there will not be any more data on
		 * this #Istream.
		 */
		NO,

		/**
		 * More data will be available eventually and
		 * IstreamHandler::OnIstreamReady() will be called.
		 */
		PUSH,

		/**
		 * More data is available now (but may require doing
		 * synchronous I/O).  Call Istream::FillBucketList()
		 * again (after consuming the data that was just
		 * returned).
		 */
		PULL,

		/**
		 * More data is available now without any I/O (but may
		 * not fit into this object).  Call
		 * Istream::FillBucketList() again (after consuming
		 * the data that was just returned).
		 */
		AGAIN,

		/**
		 * Is the producer unable to produce more bucket data,
		 * i.e. shall the consumer fall back to
		 * Istream::Read() instead of
		 * Istream::FillBucketList()?
		 */
		FALLBACK,
	};

private:
	More more = More::NO;

public:
	constexpr IstreamBucketList() = default;

	IstreamBucketList(const IstreamBucketList &) = delete;
	IstreamBucketList &operator=(const IstreamBucketList &) = delete;

	constexpr More GetMore() const noexcept {
		return more;
	}

	constexpr void UpdateMore(More _more) noexcept {
		if (_more > more)
			more = _more;
	}

	/**
	 * @see More::PUSH
	 */
	constexpr void SetPushMore() noexcept {
		if (more < More::PUSH)
			more = More::PUSH;
	}

	/**
	 * @see More::PULL
	 */
	constexpr void SetPullMore() noexcept {
		if (more < More::PULL)
			more = More::PULL;
	}

	/**
	 * More data is available now but does not fit into this
	 * object.
	 */
	constexpr void SetTruncated() noexcept {
		UpdateMore(More::AGAIN);
	}

	constexpr bool HasMore() const noexcept {
		return more != More::NO;
	}

	constexpr void EnableFallback() noexcept {
		more = More::FALLBACK;
	}

	constexpr void CopyMoreFlagsFrom(const IstreamBucketList &src) noexcept {
		more = src.more;
	}

	constexpr void ResetMoreFlags() noexcept {
		more = More::NO;
	}

	/**
	 * @see More::FALLBACK
	 */
	constexpr bool ShouldFallback() const noexcept {
		return more == More::FALLBACK;
	}

	constexpr bool IsEmpty() const noexcept {
		return list.empty();
	}

	constexpr bool IsFull() const noexcept {
		return list.full();
	}

	constexpr void Clear() noexcept {
		list.clear();
	}

	constexpr void Push(const IstreamBucket &bucket) noexcept {
		if (IsFull()) {
			SetTruncated();
			return;
		}

		list.push_back(bucket);
	}

	constexpr void Push(std::span<const std::byte> buffer) noexcept {
		Push(IstreamBucket{buffer});
	}

	struct Marker {
		List::size_type value;
	};

	constexpr Marker Mark() const noexcept {
		return {list.size() - HasMore()};
	}

	constexpr bool EmptySinceMark(const Marker m) const noexcept {
		return !HasMore() && list.size() == m.value;
	}

	using const_iterator = List::const_iterator;

	constexpr const_iterator begin() const noexcept {
		return list.begin();
	}

	constexpr const_iterator end() const noexcept {
		return list.end();
	}

	[[gnu::pure]]
	constexpr bool HasNonBuffer() const noexcept {
		for (const auto &bucket : list)
			if (!bucket.IsBuffer())
				return true;
		return false;
	}

	[[gnu::pure]]
	constexpr size_t GetTotalBufferSize() const noexcept {
		size_t size = 0;
		for (const auto &bucket : list)
			if (bucket.IsBuffer())
				size += bucket.GetBuffer().size();
		return size;
	}

	[[gnu::pure]]
	constexpr bool IsDepleted(size_t consumed) const noexcept {
		return !HasMore() && consumed == GetTotalBufferSize();
	}

	void SpliceFrom(IstreamBucketList &&src) noexcept;

	/**
	 * Move buffer buckets from the given list, stopping at the first
	 * no-buffer bucket or after #max_size bytes have been moved.
	 *
	 * If enough data (#max_size) was found and moved, this
	 * object's "more" flags are not enabled.
	 *
	 * @return the number of bytes in all moved buffers; if
	 * #max_size was copied and there is more data in #src, the
	 * return value is #max_size+1
	 */
	size_t SpliceBuffersFrom(IstreamBucketList &&src,
				 size_t max_size) noexcept;

	/**
	 * Move buffer buckets from the given list, stopping at the first
	 * no-buffer bucket.
	 *
	 * @return the number of bytes in all moved buffers
	 */
	size_t SpliceBuffersFrom(IstreamBucketList &&src) noexcept;

	/**
	 * Copy buffer buckets from the given list, stopping at the first
	 * no-buffer bucket.
	 *
	 * @param skip skip this number of bytes at the beginning
	 * @return the number of bytes in all moved buffers
	 */
	size_t CopyBuffersFrom(size_t skip,
			       const IstreamBucketList &src) noexcept;

	/**
	 * Convert to an array of struct iovec.
	 */
	[[gnu::pure]]
	StaticVector<struct iovec, CAPACITY> ToIovec() const noexcept;
};
