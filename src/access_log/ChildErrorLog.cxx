// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ChildErrorLog.hxx"
#include "ChildErrorLogOptions.hxx"
#include "spawn/Prepared.hxx"
#include "event/net/log/PipeAdapter.hxx"
#include "io/FdHolder.hxx"
#include "io/Pipe.hxx"

#include <cassert>

ChildErrorLog::ChildErrorLog() = default;
ChildErrorLog::ChildErrorLog(ChildErrorLog &&) noexcept = default;
ChildErrorLog::~ChildErrorLog() noexcept = default;
ChildErrorLog &ChildErrorLog::operator=(ChildErrorLog &&) noexcept = default;

ChildErrorLog::ChildErrorLog(PreparedChildProcess &p, FdHolder &close_fds,
			     EventLoop &event_loop, Net::Log::Sink *sink,
			     const ChildErrorLogOptions &options,
			     bool force)
{
	EnableClient(p, close_fds,
		     event_loop, sink, options, force);
}

UniqueFileDescriptor
ChildErrorLog::EnableClient(EventLoop &event_loop, Net::Log::Sink *sink,
			    const ChildErrorLogOptions &options,
			    bool force)
{
	assert(!adapter);

	if (!options.is_default && !force)
		return UniqueFileDescriptor{};

	if (sink == nullptr)
		return UniqueFileDescriptor{};

	auto [r, w] = CreatePipe();

	/* this should not be necessary because Net::Log::PipeAdapter
	   reads only after epoll signals that the pipe is readable,
	   but we saw blocking reads on several servers, no idea why -
	   so to be 100% sure, we waste one extra system call to make
	   one pipe end non-blocking */
	r.SetNonBlocking();

	adapter = std::make_unique<Net::Log::PipeAdapter>(event_loop, std::move(r),
							  *sink,
							  Net::Log::Type::HTTP_ERROR);
	if (options.rate_limit.rate > 0)
		adapter->SetRateLimit(options.rate_limit);
	return std::move(w);
}

void
ChildErrorLog::EnableClient(PreparedChildProcess &p, FdHolder &close_fds,
			    EventLoop &event_loop, Net::Log::Sink *sink,
			    const ChildErrorLogOptions &options,
			    bool force)
{
	assert(!adapter);

	if (p.stderr_fd.IsDefined())
		/* already set */
		return;

	auto w = EnableClient(event_loop, sink, options, force);
	if (w.IsDefined()) {
		p.stderr_fd = close_fds.Insert(std::move(w));

		/* if there's nothing else on stdout (no pipe etc.),
		   redirect it to Pond as well */
		if (!p.stdout_fd.IsDefined())
			p.stdout_fd = p.stderr_fd;
	}
}

Net::Log::Datagram &
ChildErrorLog::GetDatagram() noexcept
{
	assert(adapter);

	return adapter->GetDatagram();
}

void
ChildErrorLog::SetSite(const char *_site) noexcept
{
	if (!adapter)
		return;

	if (_site == nullptr) {
		if (site.empty())
			return;

		site.clear();
	} else {
		if (site == _site)
			return;

		site = _site;
		_site = site.c_str();
	}

	GetDatagram().site = _site;
}

void
ChildErrorLog::SetUri(const char *_uri) noexcept
{
	if (!adapter)
		return;

	if (_uri == nullptr) {
		if (uri.empty())
			return;

		uri.clear();
		GetDatagram().http_uri = {};
	} else {
		std::string_view __uri{_uri};
		constexpr std::size_t max_length = 512;
		const bool truncated = __uri.size() > max_length;
		if (truncated)
			__uri = __uri.substr(0, max_length);

		if (uri == __uri)
			return;

		GetDatagram().http_uri = __uri;
		GetDatagram().truncated_http_uri = truncated;
	}
}
