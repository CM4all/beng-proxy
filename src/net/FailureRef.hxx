// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "FailureInfo.hxx"

#include <utility>

class ReferencedFailureInfo : public FailureInfo {
	unsigned refs = 1;

public:
	void Ref() noexcept {
		++refs;
	}

	void Unref() noexcept {
		if (--refs == 0)
			Destroy();
	}

	struct UnrefDisposer {
		void operator()(ReferencedFailureInfo *failure) const noexcept {
			failure->Unref();
		}
	};

protected:
	virtual void Destroy() noexcept = 0;
};

/**
 * Holds a (counted) reference to a #FailureInfo instance.
 */
class FailureRef {
	friend class FailurePtr;

	ReferencedFailureInfo &info;

public:
	explicit FailureRef(ReferencedFailureInfo &_info) noexcept;
	~FailureRef() noexcept;

	FailureRef(const FailureRef &) = delete;
	FailureRef &operator=(const FailureRef &) = delete;

	FailureInfo *operator->() const noexcept {
		return &info;
	}

	FailureInfo &operator*() const noexcept {
		return info;
	}
};

/**
 * Like #FailureRef, but manages a dynamic pointer.
 */
class FailurePtr {
	ReferencedFailureInfo *info = nullptr;

public:
	FailurePtr() = default;

	explicit FailurePtr(ReferencedFailureInfo &_info) noexcept
		:info(&_info) {
		info->Ref();
	}

	FailurePtr(FailurePtr &&src) noexcept
		:info(std::exchange(src.info, nullptr)) {}

	~FailurePtr() noexcept {
		if (info != nullptr)
			info->Unref();
	}

	FailurePtr(const FailurePtr &) = delete;
	FailurePtr &operator=(const FailurePtr &) = delete;

	operator bool() const noexcept {
		return info != nullptr;
	}

	FailurePtr &operator=(FailurePtr &&src) noexcept {
		using std::swap;
		swap(info, src.info);
		return *this;
	}

	FailurePtr &operator=(ReferencedFailureInfo &new_info) noexcept {
		if (info != nullptr)
			info->Unref();
		info = &new_info;
		info->Ref();
		return *this;
	}

	FailurePtr &operator=(const FailureRef &new_ref) noexcept {
		return *this = new_ref.info;
	}

	FailureInfo *operator->() const noexcept {
		return info;
	}

	ReferencedFailureInfo &operator*() const noexcept {
		return *info;
	}
};
