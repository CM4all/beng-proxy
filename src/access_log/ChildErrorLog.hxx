// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <memory>
#include <string>

struct ChildErrorLogOptions;
struct PreparedChildProcess;
class EventLoop;
class FdHolder;
class SocketDescriptor;
class UniqueFileDescriptor;
namespace Net::Log { struct Datagram; class PipeAdapter; class Sink; }

/**
 * A glue class which manages where a child process logs its "stderr".
 */
class ChildErrorLog {
	std::unique_ptr<Net::Log::PipeAdapter> adapter;

	std::string site, uri;

public:
	ChildErrorLog();

	/**
	 * Construct a #Net::Log::PipeAdapter if the given socket is
	 * defined.
	 *
	 * Throws on error.
	 */
	ChildErrorLog(PreparedChildProcess &p, FdHolder &close_fds,
		      EventLoop &event_loop, Net::Log::Sink *sink,
		      const ChildErrorLogOptions &options,
		      bool force);

	~ChildErrorLog() noexcept;

	ChildErrorLog(ChildErrorLog &&) noexcept;
	ChildErrorLog &operator=(ChildErrorLog &&) noexcept;

	operator bool() const {
		return adapter != nullptr;
	}

	/**
	 * @see Net::Log::PipeAdapter::GetDatagram()
	 */
	Net::Log::Datagram &GetDatagram() noexcept;

	void SetSite(const char *_site) noexcept;
	void SetUri(const char *_uri) noexcept;

	/**
	 * Throws on error.
	 */
	UniqueFileDescriptor EnableClient(EventLoop &event_loop,
					  Net::Log::Sink *sink,
					  const ChildErrorLogOptions &options,
					  bool force);

	/**
	 * Throws on error.
	 */
	void EnableClient(PreparedChildProcess &p, FdHolder &close_fds,
			  EventLoop &event_loop, Net::Log::Sink *sink,
			  const ChildErrorLogOptions &log_options,
			  bool force);
};
