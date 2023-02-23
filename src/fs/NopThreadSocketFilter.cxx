// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "NopThreadSocketFilter.hxx"

void
NopThreadSocketFilter::Run(ThreadSocketFilterInternal &f)
{
	const std::scoped_lock lock{f.mutex};
	f.handshaking = false;
	f.decrypted_input.MoveFromAllowBothNull(f.encrypted_input);
	f.encrypted_output.MoveFromAllowBothNull(f.plain_output);
}
