// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <span>

struct ChildOptions;
class AllocatorPtr;

struct CgiChildParams {
	const char *executable_path;

	std::span<const char *const> args;

	const ChildOptions &options;

	unsigned parallelism, concurrency;

	bool disposable;

	CgiChildParams(const char *_executable_path,
		       std::span<const char *const> _args,
		       const ChildOptions &_options,
		       unsigned _parallelism,
		       unsigned _concurrency,
		       bool _disposable) noexcept
		:executable_path(_executable_path), args(_args),
		 options(_options),
		 parallelism(_parallelism),
		 concurrency(_concurrency),
		 disposable(_disposable) {}

	CgiChildParams(AllocatorPtr alloc, const CgiChildParams &src) noexcept;

	/**
	 * Generates a string identifying the process.  This can be
	 * used as a key in a hash table.  The string will be
	 * allocated by the specified pool.
	 */
	[[gnu::pure]]
	const char *GetStockKey(AllocatorPtr alloc) const noexcept;
};
