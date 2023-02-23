// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

#ifdef ENABLE_TRACE
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
PoolPtr::Allocate(std::size_t size) const noexcept
{
	return p_malloc(value, size);
}
