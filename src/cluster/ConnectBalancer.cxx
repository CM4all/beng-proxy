// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConnectBalancer.hxx"
#include "BalancerRequest.hxx"
#include "AddressListWrapper.hxx"
#include "AddressList.hxx"
#include "BalancerMap.hxx"
#include "net/PConnectSocket.hxx"
#include "event/Loop.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "stopwatch.hxx"

class ClientBalancerRequest : ConnectSocketHandler {
	EventLoop &event_loop;

	bool ip_transparent;
	StaticSocketAddress bind_address;

	/**
	 * The connect timeout for each attempt.
	 */
	const Event::Duration timeout;

	ConnectSocketHandler &handler;

public:
	ClientBalancerRequest(EventLoop &_event_loop,
			      bool _ip_transparent, SocketAddress _bind_address,
			      Event::Duration _timeout,
			      ConnectSocketHandler &_handler) noexcept
		:event_loop(_event_loop), ip_transparent(_ip_transparent),
		 timeout(_timeout),
		 handler(_handler) {
		if (_bind_address.IsNull() || !_bind_address.IsDefined())
			bind_address.Clear();
		else
			bind_address = _bind_address;
	}

	void Send(AllocatorPtr alloc, SocketAddress address,
		  CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
	void OnSocketConnectTimeout() noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;
};

using BR = BalancerRequest<ClientBalancerRequest,
			   BalancerMap::Wrapper<AddressListWrapper>>;

inline void
ClientBalancerRequest::Send(AllocatorPtr alloc, SocketAddress address,
			    CancellablePointer &cancel_ptr) noexcept
{
	client_socket_new(event_loop, alloc, nullptr,
			  address.GetFamily(), SOCK_STREAM, 0,
			  ip_transparent,
			  bind_address,
			  address,
			  timeout,
			  *this,
			  cancel_ptr);
}

/*
 * client_socket_handler
 *
 */

void
ClientBalancerRequest::OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept
{
	auto &base = BR::Cast(*this);
	base.ConnectSuccess();

	auto &_handler = handler;
	base.Destroy();
	_handler.OnSocketConnectSuccess(std::move(fd));
}

void
ClientBalancerRequest::OnSocketConnectTimeout() noexcept
{
	auto &base = BR::Cast(*this);
	if (!base.ConnectFailure(event_loop.SteadyNow())) {
		auto &_handler = handler;
		base.Destroy();
		_handler.OnSocketConnectTimeout();
	}
}

void
ClientBalancerRequest::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	auto &base = BR::Cast(*this);
	if (!base.ConnectFailure(event_loop.SteadyNow())) {
		auto &_handler = handler;
		base.Destroy();
		_handler.OnSocketConnectError(ep);
	}
}

/*
 * constructor
 *
 */

void
client_balancer_connect(EventLoop &event_loop,
			AllocatorPtr alloc, BalancerMap &balancer,
			FailureManager &failure_manager,
			bool ip_transparent,
			SocketAddress bind_address,
			sticky_hash_t sticky_hash,
			const AddressList &address_list,
			Event::Duration timeout,
			ConnectSocketHandler &handler,
			CancellablePointer &cancel_ptr)
{
	BR::Start(alloc, event_loop.SteadyNow(),
		  balancer.MakeAddressListWrapper(AddressListWrapper(failure_manager,
								     address_list.addresses),
						  address_list.sticky_mode),
		  cancel_ptr,
		  sticky_hash,
		  event_loop,
		  ip_transparent,
		  bind_address,
		  timeout,
		  handler);
}
