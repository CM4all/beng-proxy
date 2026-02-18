// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SharedFd.hxx"

inline
SharedFd::~SharedFd() noexcept
{
	assert(fd.IsDefined());

	fd.Close();
}

void
SharedFd::OnAbandoned() noexcept
{
	delete this;
}
