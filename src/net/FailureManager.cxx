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

#include "FailureManager.hxx"
#include "FailureRef.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketAddress.hxx"
#include "util/djbhash.h"
#include "util/LeakDetector.hxx"

#include <assert.h>

class FailureManager::Failure final
	: public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
	  LeakDetector,
	  public ReferencedFailureInfo {

	const AllocatedSocketAddress address;

public:
	explicit Failure(SocketAddress _address) noexcept
		:address(_address) {}

	SocketAddress GetAddress() const noexcept {
		return address;
	}

protected:
	void Destroy() override {
		delete this;
	}
};

inline size_t
FailureManager::Hash::operator()(const SocketAddress a) const noexcept
{
	assert(!a.IsNull());

	return djb_hash(a.GetAddress(), a.GetSize());
}

inline size_t
FailureManager::Hash::operator()(const Failure &f) const noexcept
{
	return this->operator()(f.GetAddress());
}

inline bool
FailureManager::Equal::operator()(const SocketAddress a,
				  const SocketAddress b) const noexcept
{
	return a == b;
}

inline bool
FailureManager::Equal::operator()(const SocketAddress a,
				  const Failure &b) const noexcept
{
	return a == b.GetAddress();
}

FailureManager::~FailureManager() noexcept
{
	failures.clear_and_dispose(Failure::UnrefDisposer());
}

ReferencedFailureInfo &
FailureManager::Make(SocketAddress address) noexcept
{
	assert(!address.IsNull());

	FailureSet::insert_commit_data hint;
	auto result = failures.insert_check(address, Hash(), Equal(), hint);
	if (result.second) {
		Failure *failure = new Failure(address);
		failures.insert_commit(*failure, hint);
		return *failure;
	} else {
		return *result.first;
	}
}

SocketAddress
FailureManager::GetAddress(const FailureInfo &info) const noexcept
{
	const auto &f = (const Failure &)info;
	return f.GetAddress();
}

FailureStatus
FailureManager::Get(const Expiry now, SocketAddress address) const noexcept
{
	assert(!address.IsNull());

	auto i = failures.find(address, Hash(), Equal());
	if (i == failures.end())
		return FailureStatus::OK;

	return i->GetStatus(now);
}

bool
FailureManager::Check(const Expiry now, SocketAddress address,
		      bool allow_fade) const noexcept
{
	assert(!address.IsNull());

	auto i = failures.find(address, Hash(), Equal());
	if (i == failures.end())
		return true;

	return i->Check(now, allow_fade);
}
