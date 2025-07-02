// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/FineTimerEvent.hxx"
#include "net/log/Sink.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"

#include <array>

#include <sys/socket.h> // for struct mmsghdr
#include <sys/uio.h> // for struct iovec

namespace Net { namespace Log { struct Datagram; }}

/**
 * A client for the logging protocol.
 */
class LogClient final : public Net::Log::Sink {
	const LLogger logger;

	UniqueSocketDescriptor fd;

	FineTimerEvent flush_timer;

	std::array<std::byte, 32768> buffer;
	std::size_t buffer_fill = 0;

	std::array<struct iovec, 256> vecs;
	std::size_t n_vecs = 0;

public:
	explicit LogClient(EventLoop &event_loop, UniqueSocketDescriptor &&_fd) noexcept
		:logger("access_log"), fd(std::move(_fd)),
		 flush_timer(event_loop, BIND_THIS_METHOD(Flush)) {}

	SocketDescriptor GetSocket() noexcept {
		return fd;
	}

	// virtual methods from class Net::Log::Sink
	void Log(const Net::Log::Datagram &d) noexcept override;

private:
	/**
	 * @return true if the datagram was appended to the buffer,
	 * false if it was discarded
	 */
	bool Append(const Net::Log::Datagram &d) noexcept;

	/**
	 * @return true if the datagram was appended to the buffer,
	 * false if it was discarded
	 */
	bool AppendRetry(const Net::Log::Datagram &d) noexcept;

	/**
	 * Send #buffer contents to the socket.
	 */
	void Flush() noexcept;
};
