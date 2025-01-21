// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChildParams.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/StringBuilder.hxx"
#include "stock/Key.hxx"
#include "util/djb_hash.hxx"
#include "util/IntHash.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"

CgiChildParams::CgiChildParams(AllocatorPtr alloc, const CgiChildParams &src) noexcept
	:executable_path(alloc.Dup(src.executable_path)),
	 args(alloc.CloneStringArray(src.args)),
	 options(*alloc.New<ChildOptions>(alloc, src.options)),
	 parallelism(src.parallelism),
	 concurrency(src.concurrency),
	 disposable(src.disposable)
{
}

inline std::size_t
CgiChildParams::GetStockHash() const noexcept
{
	std::size_t hash = INT_HASH_INIT;
	hash = IntHashUpdate(djb_hash(AsBytes(executable_path)), hash);

	for (const char *i : args)
		hash = IntHashUpdate(djb_hash_string(i), hash);

	hash = IntHashUpdate(djb_hash(AsBytes(options.tag)), hash);

	if (options.ns.mount.pivot_root != nullptr)
		hash = IntHashUpdate(djb_hash(AsBytes(options.ns.mount.pivot_root)), hash);

	if (options.ns.mount.home != nullptr)
		hash = IntHashUpdate(djb_hash(AsBytes(options.ns.mount.home)), hash);

	return hash;
}

StockKey
CgiChildParams::GetStockKey(AllocatorPtr alloc) const noexcept
{
	PoolStringBuilder<256> b;
	b.push_back(executable_path);

	for (auto i : args) {
		b.push_back(" ");
		b.push_back(i);
	}

	for (auto i : options.env) {
		b.push_back("$");
		b.push_back(i);
	}

	char options_buffer[16384];
	b.emplace_back(options_buffer,
		       options.MakeId(options_buffer));

	return StockKey{b.MakeView(alloc), GetStockHash()};
}
