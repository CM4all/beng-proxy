// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ChildParams.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Prepared.hxx"
#include "pool/StringBuilder.hxx"
#include "stock/Key.hxx"
#include "util/djb_hash.hxx"
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

void
CgiChildParams::CopyTo(PreparedChildProcess &dest, FdHolder &close_fds) const noexcept
{
	dest.Append(executable_path);

	for (const char *i : args)
		dest.Append(i);

	options.CopyTo(dest, close_fds);
}
