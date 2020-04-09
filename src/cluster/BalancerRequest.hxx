/*
 * Copyright 2007-2020 Content Management AG
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

#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

#include <utility>

class CancellablePointer;

/**
 * Generic connection balancer.
 */
template<class R, typename List>
class BalancerRequest final : Cancellable {
	R request;

	const AllocatorPtr alloc;

	const List list;

	CancellablePointer cancel_ptr;

	/**
	 * The "sticky id" of the incoming HTTP request.
	 */
	const sticky_hash_t sticky_hash;

	/**
	 * The number of remaining connection attempts.  We give up when
	 * we get an error and this attribute is already zero.
	 */
	unsigned retries;

	FailurePtr failure;

public:
	template<typename... Args>
	BalancerRequest(AllocatorPtr _alloc,
			List &&_list,
			CancellablePointer &_cancel_ptr,
			sticky_hash_t _sticky_hash,
			Args&&... args) noexcept
		:request(std::forward<Args>(args)...),
		 alloc(_alloc),
		 list(std::move(_list)),
		 sticky_hash(_sticky_hash),
		 retries(CalculateRetries(list))
	{
		_cancel_ptr = *this;
	}

	BalancerRequest(const BalancerRequest &) = delete;

	void Destroy() noexcept {
		this->~BalancerRequest();
	}

	auto &GetFailureInfo() noexcept {
		return *failure;
	}

private:
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	static unsigned CalculateRetries(const List &list) noexcept {
		const unsigned size = list.size();
		if (size <= 1)
			return 0;
		else if (size == 2)
			return 1;
		else if (size == 3)
			return 2;
		else
			return 3;
	}

public:
	static constexpr BalancerRequest &Cast(R &r) noexcept {
		return ContainerCast(r, &BalancerRequest::request);
	}

	void Next(Expiry now) noexcept {
		auto current_address = list.Pick(now, sticky_hash);

		failure = list.MakeFailureInfo(current_address);
		request.Send(alloc, std::move(current_address), cancel_ptr);
	}

	void ConnectSuccess() noexcept {
		failure->UnsetConnect();
	}

	bool ConnectFailure(Expiry now) noexcept {
		failure->SetConnect(now, std::chrono::seconds(20));

		if (retries-- > 0){
			/* try again, next address */
			Next(now);
			return true;
		} else
			/* give up */
			return false;
	}

	template<typename... Args>
	static void Start(AllocatorPtr alloc, Expiry now,
			  Args&&... args) noexcept {
		auto r = alloc.New<BalancerRequest>(alloc,
						    std::forward<Args>(args)...);
		r->Next(now);
	}
};
