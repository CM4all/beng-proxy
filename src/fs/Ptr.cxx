// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Ptr.hxx"
#include "SocketFilter.hxx"

void
SocketFilterDisposer::operator()(SocketFilter *f) const noexcept
{
	f->Close();
}
