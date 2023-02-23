// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Context.hxx"

#ifdef HAVE_AVAHI

#include "fs/Stock.hxx"
#include "lib/avahi/Client.hxx"

Avahi::Client &
LbContext::GetAvahiClient() const noexcept
{
	if (!avahi_client)
		avahi_client = std::make_unique<Avahi::Client>(fs_stock.GetEventLoop(),
							       avahi_error_handler);

	return *avahi_client;
}

#endif
