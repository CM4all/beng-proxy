// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Capabilities.hxx"
#include "lib/cap/State.hxx"
#include "system/Error.hxx"

void
capabilities_init()
{
	/* don't inherit any capabilities to spawned processes */
	auto state = CapabilityState::Current();
	state.ClearFlag(CAP_INHERITABLE);
	state.Install();
}

void
capabilities_post_setuid(std::span<const cap_value_t> keep_list)
{
	/* drop all capabilities but the ones we want */

	CapabilityState state = CapabilityState::Empty();

	if (!keep_list.empty()) {
		state.SetFlag(CAP_EFFECTIVE, keep_list, CAP_SET);
		state.SetFlag(CAP_PERMITTED, keep_list, CAP_SET);
	}

	state.Install();
}
