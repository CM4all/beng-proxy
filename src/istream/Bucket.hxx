// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/StaticVector.hxx"

#include <cassert>
#include <span>

class IstreamBucket {
public:
	enum class Type {
		BUFFER,
	};

private:
	Type type;

	union {
		std::span<const std::byte> buffer;
	};

public:
	explicit IstreamBucket(std::span<const std::byte> _buffer) noexcept
		:type(Type::BUFFER),
		 buffer(_buffer) {}


	Type GetType() const noexcept {
		return type;
	}

	bool IsBuffer() const noexcept {
		return type == Type::BUFFER;
	}

	std::span<const std::byte> GetBuffer() const noexcept {
		assert(type == Type::BUFFER);

		return buffer;
	}
};

class IstreamBucketList {
	using List = StaticVector<IstreamBucket, 64>;
	List list;

	bool more = false;

	bool fallback = false;

public:
	IstreamBucketList() = default;

	IstreamBucketList(const IstreamBucketList &) = delete;
	IstreamBucketList &operator=(const IstreamBucketList &) = delete;

	void SetMore() noexcept {
		more = true;
	}

	bool HasMore() const noexcept {
		return more;
	}

	void EnableFallback() noexcept {
		SetMore();
		fallback = true;
	}

	void DisableFallback() noexcept {
		fallback = false;
	}

	void CopyMoreFlagsFrom(const IstreamBucketList &src) noexcept {
		more = src.more;
		fallback = src.fallback;
	}

	void ResetMoreFlags() noexcept {
		more = false;
		fallback = false;
	}

	/**
	 * Is the producer unable to produce more bucket data,
	 * i.e. shall the consumer fall back to Istream::Read()
	 * instead of Istream::FillBucketList()?
	 */
	bool ShouldFallback() const noexcept {
		return fallback;
	}

	bool IsEmpty() const noexcept {
		return list.empty();
	}

	bool IsFull() const noexcept {
		return list.full();
	}

	void Clear() noexcept {
		list.clear();
	}

	void Push(const IstreamBucket &bucket) noexcept {
		if (IsFull()) {
			SetMore();
			return;
		}

		list.push_back(bucket);
	}

	void Push(std::span<const std::byte> buffer) noexcept {
		Push(IstreamBucket{buffer});
	}

	struct Marker {
		List::size_type value;
	};

	Marker Mark() const noexcept {
		return {list.size() - HasMore()};
	}

	bool EmptySinceMark(const Marker m) const noexcept {
		return !HasMore() && list.size() == m.value;
	}

	using const_iterator = List::const_iterator;

	const_iterator begin() const noexcept {
		return list.begin();
	}

	const_iterator end() const noexcept {
		return list.end();
	}

	[[gnu::pure]]
	bool HasNonBuffer() const noexcept {
		for (const auto &bucket : list)
			if (!bucket.IsBuffer())
				return true;
		return false;
	}

	[[gnu::pure]]
	size_t GetTotalBufferSize() const noexcept {
		size_t size = 0;
		for (const auto &bucket : list)
			if (bucket.IsBuffer())
				size += bucket.GetBuffer().size();
		return size;
	}

	[[gnu::pure]]
	bool IsDepleted(size_t consumed) const noexcept {
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
};
