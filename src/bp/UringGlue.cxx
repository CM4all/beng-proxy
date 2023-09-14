// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringGlue.hxx"

#ifdef HAVE_URING
#include "util/PrintException.hxx"

#include <cstdio>
#endif

UringGlue::UringGlue([[maybe_unused]] EventLoop &event_loop) noexcept
{
#ifdef HAVE_URING
	try {
		uring.emplace(event_loop);
	} catch (...) {
		fprintf(stderr, "Failed to initialize io_uring: ");
		PrintException(std::current_exception());
	}
#endif
}

void
UringGlue::SetVolatile() noexcept
{
#ifdef HAVE_URING
	if (uring)
		uring->SetVolatile();
#endif
}
