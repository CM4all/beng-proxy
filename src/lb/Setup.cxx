// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "Config.hxx"
#include "Listener.hxx"
#include "Control.hxx"
#include "lib/fmt/RuntimeError.hxx"

void
LbInstance::InitAllListeners(const UidGid *logger_user)
{
	for (const auto &i : config.listeners) {
		try {
			listeners.emplace_front(*this,
						access_log.Make(event_loop,
								config.access_log,
								logger_user,
								i.access_logger_name),
						i);
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("Failed to set up listener '{}'",
							       i.name));
		}
	}
}

void
LbInstance::DeinitAllListeners() noexcept
{
	listeners.clear();
}

void
LbInstance::InitAllControls()
{
	for (const auto &i : config.controls) {
		controls.emplace_front(*this, i);
	}
}

void
LbInstance::DeinitAllControls() noexcept
{
	controls.clear();
}

void
LbInstance::EnableAllControls() noexcept
{
	for (auto &control : controls)
		control.Enable();
}
