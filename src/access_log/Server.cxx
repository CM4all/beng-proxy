// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Server.hxx"
#include "net/log/Parser.hxx"

#include <sys/socket.h>
#include <stdlib.h>

AccessLogServer::AccessLogServer()
	:AccessLogServer(SocketDescriptor(STDIN_FILENO)) {}

bool
AccessLogServer::Fill()
{
	assert(current_payload >= n_payloads);

	std::array<struct iovec, N> iovs;
	std::array<struct mmsghdr, N> msgs;

	for (size_t i = 0; i < N; ++i) {
		auto &iov = iovs[i];
		iov.iov_base = payloads[i];
		iov.iov_len = sizeof(payloads[i]) - 1;

		auto &msg = msgs[i].msg_hdr;
		msg.msg_name = (struct sockaddr *)addresses[i];
		msg.msg_namelen = addresses[i].GetCapacity();
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = nullptr;
		msg.msg_controllen = 0;
	}

	int n = recvmmsg(fd.Get(), &msgs.front(), msgs.size(),
			 MSG_WAITFORONE|MSG_CMSG_CLOEXEC, nullptr);
	if (n <= 0)
		return false;

	for (n_payloads = 0; n_payloads < size_t(n); ++n_payloads) {
		if (msgs[n_payloads].msg_len == 0)
			/* when the peer closes the socket, recvmmsg() doesn't
			   return 0; instead, it fills the mmsghdr array with
			   empty packets */
			break;

		if (msgs[n_payloads].msg_hdr.msg_namelen >= sizeof(struct sockaddr))
			addresses[n_payloads].SetSize(msgs[n_payloads].msg_hdr.msg_namelen);
		else
			addresses[n_payloads].Clear();

		sizes[n_payloads] = msgs[n_payloads].msg_len;
	}

	current_payload = 0;
	return n_payloads > 0;
}

const ReceivedAccessLogDatagram *
AccessLogServer::Receive()
{
	while (true) {
		if (current_payload >= n_payloads && !Fill())
			return nullptr;

		assert(current_payload < n_payloads);

		const SocketAddress address = addresses[current_payload];
		std::byte *buffer = payloads[current_payload];
		size_t nbytes = sizes[current_payload];
		++current_payload;

		/* force null termination so we can use string functions inside
		   the buffer */
		buffer[nbytes] = std::byte{'\0'};

		datagram.logger_client_address = address;
		datagram.raw = {buffer, nbytes};

		try {
			Net::Log::Datagram &base = datagram;
			base = Net::Log::ParseDatagram({buffer, nbytes});
			return &datagram;
		} catch (Net::Log::ProtocolError) {
		}
	}
}

