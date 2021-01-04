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

#include "Ptr.hxx"
#include "pool.hxx"

PoolPtr::PoolPtr(struct pool &_value TRACE_ARGS_DECL_) noexcept
	:value(&_value)
	 TRACE_ARGS_INIT
{
	pool_ref(value TRACE_ARGS_FWD);
}

PoolPtr::PoolPtr(const PoolPtr &src TRACE_ARGS_DECL_) noexcept
	:value(src.value)
	 TRACE_ARGS_INIT
{
	if (value != nullptr)
		pool_ref(value TRACE_ARGS_FWD);
}

PoolPtr::~PoolPtr() noexcept
{
	if (value != nullptr)
		pool_unref(value TRACE_ARGS_FWD);
}

PoolPtr &
PoolPtr::operator=(const PoolPtr &src) noexcept
{
	if (value != nullptr)
		pool_unref(value);
	value = src.value;
	if (value != nullptr)
		pool_ref(value);

#ifdef TRACE
	file = src.file;
	line = src.line;
#endif

	return *this;
}

void
PoolPtr::reset() noexcept
{
	if (value != nullptr)
		pool_unref(std::exchange(value, nullptr) TRACE_ARGS_FWD);
}

void *
PoolPtr::Allocate(size_t size) const noexcept
{
	return p_malloc(value, size);
}
