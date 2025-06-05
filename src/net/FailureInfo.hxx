// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "FailureStatus.hxx"
#include "time/Expiry.hxx"

class FailureInfo {
	Expiry fade_expires = Expiry::AlreadyExpired();

	Expiry protocol_expires = Expiry::AlreadyExpired();

	Expiry connect_expires = Expiry::AlreadyExpired();

	unsigned protocol_counter = 0;

	bool monitor = false;

public:
	constexpr FailureStatus GetStatus(Expiry now) const noexcept {
		if (!CheckMonitor())
			return FailureStatus::MONITOR;
		else if (!CheckConnect(now))
			return FailureStatus::CONNECT;
		else if (!CheckProtocol(now))
			return FailureStatus::PROTOCOL;
		else if (!CheckFade(now))
			return FailureStatus::FADE;
		else
			return FailureStatus::OK;
	}

	constexpr bool Check(Expiry now, bool allow_fade=false) const noexcept {
		return CheckMonitor() &&
			CheckConnect(now) &&
			CheckProtocol(now) &&
			(allow_fade || CheckFade(now));
	}

	/**
	 * Set the specified failure status, but only if it is not less
	 * severe than the current status.
	 */
	void Set(Expiry now, FailureStatus new_status,
		 std::chrono::seconds duration) noexcept;

	/**
	 * Unset a failure status.
	 *
	 * @param status the status to be removed; #FailureStatus::OK is a
	 * catch-all status that matches everything
	 */
	void Unset(FailureStatus unset_status) noexcept;

	void SetFade(Expiry now, std::chrono::seconds duration) noexcept {
		fade_expires.Touch(now, duration);
	}

	void UnsetFade() noexcept {
		fade_expires = Expiry::AlreadyExpired();
	}

	constexpr bool CheckFade(Expiry now) const noexcept {
		return fade_expires.IsExpired(now);
	}

	void SetProtocol(Expiry now, std::chrono::seconds duration) noexcept {
		protocol_expires.Touch(now, duration);
		++protocol_counter;
	}

	void UnsetProtocol() noexcept {
		protocol_expires = Expiry::AlreadyExpired();
		protocol_counter = 0;
	}

	constexpr bool CheckProtocol(Expiry now) const noexcept {
		return protocol_expires.IsExpired(now) || protocol_counter < 8;
	}

	void SetConnect(Expiry now, std::chrono::seconds duration) noexcept {
		connect_expires.Touch(now, duration);
	}

	void UnsetConnect() noexcept {
		connect_expires = Expiry::AlreadyExpired();
	}

	constexpr bool CheckConnect(Expiry now) const noexcept {
		return connect_expires.IsExpired(now);
	}

	void SetMonitor() noexcept {
		monitor = true;
	}

	void UnsetMonitor() noexcept {
		monitor = false;
	}

	constexpr bool CheckMonitor() const noexcept {
		return !monitor;
	}

	void UnsetAll() noexcept {
		fade_expires = protocol_expires = connect_expires =
			Expiry::AlreadyExpired();
		protocol_counter = 0;
		monitor = false;
	}
};
