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

#pragma once

#include "PickFailover.hxx"
#include "PickModulo.hxx"
#include "StickyMode.hxx"
#include "RoundRobinBalancer.cxx"
#include "net/SocketAddress.hxx"
#include "util/Expiry.hxx"

/**
 * Pick an address using the given #StickyMode.
 */
template<typename List>
[[gnu::pure]]
const auto &
PickGeneric(Expiry now, StickyMode sticky_mode,
	    const List &list, sticky_hash_t sticky_hash) noexcept
{
	if (list.size() == 1)
		return *list.begin();

	switch (sticky_mode) {
	case StickyMode::NONE:
		break;

	case StickyMode::FAILOVER:
		return PickFailover(now, list);

	case StickyMode::SOURCE_IP:
	case StickyMode::HOST:
	case StickyMode::XHOST:
	case StickyMode::SESSION_MODULO:
	case StickyMode::COOKIE:
	case StickyMode::JVM_ROUTE:
		if (sticky_hash != 0)
			return PickModulo(now, list,
					  sticky_hash);

		break;
	}

	return list.GetRoundRobinBalancer().Get(now, list,
						sticky_mode == StickyMode::NONE);
}
