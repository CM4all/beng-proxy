// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailureInfo.hxx"

void
FailureInfo::Set(Expiry now,
		 FailureStatus new_status,
		 std::chrono::seconds duration) noexcept
{
	switch (new_status) {
	case FailureStatus::OK:
		break;

	case FailureStatus::FADE:
		SetFade(now, duration);
		break;

	case FailureStatus::PROTOCOL:
		SetProtocol(now, duration);
		break;

	case FailureStatus::CONNECT:
		SetConnect(now, duration);
		break;

	case FailureStatus::MONITOR:
		SetMonitor();
		break;
	}
}

void
FailureInfo::Unset(FailureStatus unset_status) noexcept
{
	switch (unset_status) {
	case FailureStatus::OK:
		UnsetAll();
		break;

	case FailureStatus::FADE:
		UnsetFade();
		break;

	case FailureStatus::PROTOCOL:
		UnsetProtocol();
		break;

	case FailureStatus::CONNECT:
		UnsetConnect();
		break;

	case FailureStatus::MONITOR:
		UnsetMonitor();
		break;
	}
}
