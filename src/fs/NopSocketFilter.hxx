// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "SocketFilter.hxx"

/**
 * A module for #FilteredSocket that does not filter anything.  It
 * passes data as-is.  It is meant for debugging.
 */
class NopSocketFilter final : public SocketFilter {
	FilteredSocket *socket;

public:
	/* virtual methods from SocketFilter */
	void Init(FilteredSocket &_socket) noexcept override {
		socket = &_socket;
	}

	BufferedResult OnData() noexcept override;
	bool IsEmpty() const noexcept override;
	bool IsFull() const noexcept override;
	std::size_t GetAvailable() const noexcept override;
	std::span<std::byte> ReadBuffer() noexcept override;
	void Consumed(std::size_t nbytes) noexcept override;
	void AfterConsumed() noexcept override;
	BufferedReadResult Read() noexcept override;
	ssize_t Write(std::span<const std::byte> src) noexcept override;
	void ScheduleRead() noexcept override;
	void ScheduleWrite() noexcept override;
	void UnscheduleWrite() noexcept override;
	bool InternalWrite() noexcept override;
	bool OnRemaining(std::size_t remaining) noexcept override;
	void OnEnd() override;
	void Close() noexcept override;
};
