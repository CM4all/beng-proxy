/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Listener.hxx"
#include "FilteredSocket.hxx"
#include "Ptr.hxx"
#include "ThreadSocketFilter.hxx"
#include "pool/Holder.hxx"
#include "pool/Ptr.hxx"
#include "pool/UniquePtr.hxx"
#include "pool/PSocketAddress.hxx"
#include "ssl/Factory.hxx"
#include "ssl/Filter.hxx"
#include "thread/Pool.hxx"
#include "net/SocketAddress.hxx"
#include "io/FdType.hxx"

class FilteredSocketListener::Pending final
	: PoolHolder,
	  public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>,
	  BufferedSocketHandler
{
	UniquePoolPtr<FilteredSocket> socket;

	const SocketAddress address;

	const SslFilter *const ssl_filter;

	FilteredSocketListenerHandler &handler;

public:
	Pending(PoolPtr &&_pool,
		UniquePoolPtr<FilteredSocket> &&_socket,
		SocketAddress _address,
		const SslFilter *_ssl_filter,
		FilteredSocketListenerHandler &_handler) noexcept
		:PoolHolder(std::move(_pool)),
		 socket(std::move(_socket)),
		 address(DupAddress((AllocatorPtr)pool, _address)),
		 ssl_filter(_ssl_filter), handler(_handler)
	{
		socket->Reinit(Event::Duration(-1),
			       Event::Duration(-1),
			       *this);
	}

	void Destroy() noexcept {
		this->~Pending();
	}

	void Start() noexcept {
		socket->ScheduleReadNoTimeout(false);
		socket->SetHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
	}

private:
	void OnHandshake() noexcept {
		handler.OnFilteredSocketConnect(pool,
						std::move(socket),
						address,
						ssl_filter);
		Destroy();
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override {
		return BufferedResult::BLOCKING;
	}

	bool OnBufferedClosed() noexcept override {
		Destroy();
		return false;
	}

	bool OnBufferedWrite() override {
		return true;
	}

	void OnBufferedError(std::exception_ptr) noexcept override {
		Destroy();
	}
};

FilteredSocketListener::FilteredSocketListener(struct pool &_pool,
					       EventLoop &event_loop,
					       std::unique_ptr<SslFactory> _ssl_factory,
					       FilteredSocketListenerHandler &_handler) noexcept
	:ServerSocket(event_loop),
	 parent_pool(_pool), ssl_factory(std::move(_ssl_factory)), handler(_handler)
{
}

FilteredSocketListener::~FilteredSocketListener() noexcept
{
	pending.clear_and_dispose(NoPoolDisposer{});
}

unsigned
FilteredSocketListener::FlushSSLSessionCache(long tm) noexcept
{
	return ssl_factory != nullptr
		? ssl_factory->Flush(tm)
		: 0;
}

void
FilteredSocketListener::OnAccept(UniqueSocketDescriptor &&s,
				 SocketAddress address) noexcept
try {
	auto &event_loop = GetEventLoop();

	auto connection_pool = pool_new_linear(&parent_pool, "connection", 2048);
	pool_set_major(connection_pool);

	const auto fd_type = FdType::FD_TCP;

	if (ssl_factory == nullptr) {
		/* quick path: no filter, no need to wait for handshake */
		auto socket = UniquePoolPtr<FilteredSocket>::Make(connection_pool,
								  event_loop,
								  std::move(s), fd_type,
								  nullptr);

		handler.OnFilteredSocketConnect(std::move(connection_pool),
						std::move(socket),
						address,
						nullptr);
		return;
	}

	auto *ssl_filter = ssl_filter_new(*ssl_factory);
	SocketFilterPtr filter(new ThreadSocketFilter(event_loop,
						      thread_pool_get_queue(event_loop),
						      &ssl_filter_get_handler(*ssl_filter)));

	auto socket = UniquePoolPtr<FilteredSocket>::Make(connection_pool,
							  event_loop,
							  std::move(s), fd_type,
							  std::move(filter));

	auto *p = NewFromPool<Pending>(std::move(connection_pool),
				       std::move(socket),
				       address, ssl_filter, handler);
	pending.push_front(*p);

	p->Start();
} catch (...) {
	/* catch errors from ssl_filter_new() */
	handler.OnFilteredSocketError(std::current_exception());
}

void
FilteredSocketListener::OnAcceptError(std::exception_ptr e) noexcept
{
	handler.OnFilteredSocketError(std::move(e));
}
