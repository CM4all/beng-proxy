// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
#include "net/IPv4Address.hxx"
#include "net/SocketAddress.hxx"
#include "io/FdType.hxx"

UniqueSocketDescriptor
FilteredSocketListenerHandler::OnFilteredSocketAccept(UniqueSocketDescriptor s,
						      SocketAddress)
{
	return s;
}

class FilteredSocketListener::Pending final
	: PoolHolder,
	  public AutoUnlinkIntrusiveListHook,
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
		socket->Reinit(Event::Duration(-1), *this);
	}

	void Destroy() noexcept {
		this->~Pending();
	}

	void Start() noexcept {
		socket->ScheduleRead();
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
		return BufferedResult::OK;
	}

	bool OnBufferedHangup() noexcept override {
		Destroy();
		return false;
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
#ifdef HAVE_URING
					       Uring::Queue *_uring_queue,
#endif
					       FilteredSocketListenerHandler &_handler,
					       UniqueSocketDescriptor _socket) noexcept
	:ServerSocket(event_loop, std::move(_socket)),
	 parent_pool(_pool),
	 ssl_factory(std::move(_ssl_factory)),
#ifdef HAVE_URING
	 uring_queue(_uring_queue),
#endif
	 handler(_handler)
{
}

FilteredSocketListener::~FilteredSocketListener() noexcept
{
	pending.clear_and_dispose(NoPoolDisposer{});
}

void
FilteredSocketListener::OnAccept(UniqueSocketDescriptor s,
				 SocketAddress address) noexcept
try {
	IPv4Address ipv4_buffer;
	if (address.IsDefined() && address.IsV4Mapped())
		address = ipv4_buffer = address.UnmapV4();

	s = handler.OnFilteredSocketAccept(std::move(s), address);
	if (!s.IsDefined())
		return;

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

#ifdef HAVE_URING
		if (uring_queue != nullptr)
			socket->EnableUring(*uring_queue);
#endif

		handler.OnFilteredSocketConnect(std::move(connection_pool),
						std::move(socket),
						address,
						nullptr);
		return;
	}

	auto f = ssl_filter_new(ssl_factory->Make());
	auto &ssl_filter = ssl_filter_cast_from(*f);

	SocketFilterPtr filter(new ThreadSocketFilter(thread_pool_get_queue(event_loop),
						      std::move(f)));

	auto socket = UniquePoolPtr<FilteredSocket>::Make(connection_pool,
							  event_loop,
							  std::move(s), fd_type,
							  std::move(filter));
#ifdef HAVE_URING
	if (uring_queue != nullptr)
		socket->EnableUring(*uring_queue);
#endif

	auto *p = NewFromPool<Pending>(std::move(connection_pool),
				       std::move(socket),
				       address, &ssl_filter, handler);
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
