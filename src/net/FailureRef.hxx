/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
	virtual void Destroy() = 0;
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

	operator bool() const {
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
