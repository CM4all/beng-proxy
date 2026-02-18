// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SharedFd.hxx"

#ifdef HAVE_URING
#include "io/uring/Close.hxx"
#endif

inline
SharedFd::~SharedFd() noexcept
{
	assert(fd.IsDefined());

#ifdef HAVE_URING
	Uring::Close(uring, fd);
#else
	fd.Close();
#endif
}

void
SharedFd::OnAbandoned() noexcept
{
	delete this;
}
