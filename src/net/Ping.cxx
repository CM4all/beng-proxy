// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Ping.hxx"
#include "net/IPv4Address.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/SendMessage.hxx"
#include "io/Iovec.hxx"

#include <cassert>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>

PingClient::PingClient(EventLoop &event_loop,
		       PingClientHandler &_handler) noexcept
	:event(event_loop, BIND_THIS_METHOD(EventCallback)),
	 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
	 handler(_handler)
{
}

inline void
PingClient::ScheduleRead() noexcept
{
	event.ScheduleRead();
	timeout_event.Schedule(std::chrono::seconds(10));
}

static u_short
in_cksum(const u_short *addr, int len, u_short csum) noexcept
{
	int nleft = len;
	const u_short *w = addr;
	u_short answer;
	int sum = csum;

	/*
	 *  Our algorithm is simple, using a 32 bit accumulator (sum),
	 *  we add sequential 16 bit words to it, and at the end, fold
	 *  back all the carry bits from the top 16 bits into the lower
	 *  16 bits.
	 */
	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	/* mop up an odd byte, if necessary */
	if (nleft == 1)
		sum += htons(*(const u_char *)w << 8);

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return (answer);
}

static bool
parse_reply(struct msghdr *msg, size_t cc, uint16_t ident) noexcept
{
	const void *buf = (const void *)msg->msg_iov->iov_base;
	const struct icmphdr *icp = (const struct icmphdr *)buf;
	if (cc < sizeof(*icp))
		return false;

	return icp->type == ICMP_ECHOREPLY && icp->un.echo.id == ident;
}

inline void
PingClient::Read() noexcept
{
	char buffer[1024];
	auto iov = MakeIovecT(buffer);

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));

	char addrbuf[128];
	msg.msg_name = addrbuf;
	msg.msg_namelen = sizeof(addrbuf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	char ans_data[4096];
	msg.msg_control = ans_data;
	msg.msg_controllen = sizeof(ans_data);

	int cc = recvmsg(fd.Get(), &msg, MSG_DONTWAIT);
	if (cc >= 0) {
		if (parse_reply(&msg, cc, ident)) {
			fd.Close();
			handler.PingResponse();
		} else
			ScheduleRead();
	} else if (const auto e = GetSocketError(); IsSocketErrorReceiveWouldBlock(e)) {
		ScheduleRead();
	} else {
		fd.Close();
		handler.PingError(std::make_exception_ptr(MakeSocketError(e, "Failed to receive ping reply")));
	}
}


/*
 * libevent callback
 *
 */

inline void
PingClient::EventCallback(unsigned) noexcept
{
	assert(fd.IsDefined());

	Read();
}

inline void
PingClient::OnTimeout() noexcept
{
	assert(fd.IsDefined());

	fd.Close();
	handler.PingTimeout();
}

/*
 * constructor
 *
 */

bool
ping_available(void) noexcept
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

static UniqueSocketDescriptor
CreateIcmp()
{
	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(AF_INET, SOCK_DGRAM, IPPROTO_ICMP))
		throw MakeSocketError("Failed to create ICMP socket");

	return fd;
}

static uint16_t
MakeIdent(SocketDescriptor fd)
{
	if (!fd.Bind(IPv4Address(0)))
		throw MakeSocketError("Failed to bind ICMP socket");

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	socklen_t sin_length = sizeof(sin);

	if (getsockname(fd.Get(), (struct sockaddr *)&sin, &sin_length) < 0)
		throw MakeSocketError("Failed to inspect ICMP socket");

	return sin.sin_port;
}

static void
SendPing(SocketDescriptor fd, SocketAddress address, uint16_t ident)
{
	struct {
		struct icmphdr header;
		char data[8];
	} packet;

	packet.header.type = ICMP_ECHO;
	packet.header.code = 0;
	packet.header.checksum = 0;
	packet.header.un.echo.sequence = htons(1);
	packet.header.un.echo.id = ident;
	memset(packet.data, 0, sizeof(packet.data));
	packet.header.checksum = in_cksum((u_short *)&packet, sizeof(packet), 0);

	const std::array iov{MakeIovecT(packet)};

	SendMessage(fd,
		    MessageHeader(iov)
		    .SetAddress(address), 0);
}

void
PingClient::Start(SocketAddress address) noexcept
{
	try {
		fd = CreateIcmp();
		event.Open(fd);
		ident = MakeIdent(fd);
		SendPing(fd, address, ident);
	} catch (...) {
		handler.PingError(std::current_exception());
		return;
	}

	ScheduleRead();
}
