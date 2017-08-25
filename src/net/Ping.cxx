/*
 * Copyright 2007-2017 Content Management AG
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

#include "Ping.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "net/IPv4Address.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
#include "util/Cancellable.hxx"

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>

class PingClient final : Cancellable {
    struct pool &pool;

    UniqueSocketDescriptor fd;

    const uint16_t ident;

    SocketEvent event;

    PingClientHandler &handler;

public:
    PingClient(EventLoop &event_loop, struct pool &_pool,
               UniqueSocketDescriptor &&_fd, uint16_t _ident,
               PingClientHandler &_handler,
               CancellablePointer &cancel_ptr)
        :pool(_pool), fd(std::move(_fd)), ident(_ident),
         event(event_loop, fd.Get(), SocketEvent::READ,
               BIND_THIS_METHOD(EventCallback)),
         handler(_handler) {
        cancel_ptr = *this;
    }

    void Destroy() {
        DeleteUnrefPool(pool, this);
    }

    void ScheduleRead() {
        event.Add(EventDuration<10>::value);
    }

private:
    void EventCallback(unsigned events);

    void Read();

    /* virtual methods from class Cancellable */
    void Cancel() override;
};

static u_short
in_cksum(const u_short *addr, register int len, u_short csum)
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

static void *
deconst_address(const struct sockaddr *address)
{
    return const_cast<struct sockaddr *>(address);
}

static bool
parse_reply(struct msghdr *msg, size_t cc, uint16_t ident)
{
    const void *buf = (const void *)msg->msg_iov->iov_base;
    const struct icmphdr *icp = (const struct icmphdr *)buf;
    if (cc < sizeof(*icp))
        return false;

    return icp->type == ICMP_ECHOREPLY && icp->un.echo.id == ident;
}

inline void
PingClient::Read()
{
    char buffer[1024];
    struct iovec iov = {
        .iov_base = buffer,
        .iov_len = sizeof(buffer),
    };

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
            Destroy();
        } else
            ScheduleRead();
    } else if (errno == EAGAIN || errno == EINTR) {
        ScheduleRead();
    } else {
        const int e = errno;
        fd.Close();
        handler.PingError(std::make_exception_ptr(MakeErrno(e)));
        Destroy();
    }
}


/*
 * libevent callback
 *
 */

inline void
PingClient::EventCallback(unsigned events)
{
    assert(fd.IsDefined());

    if (events & SocketEvent::READ) {
        Read();
    } else {
        fd.Close();
        handler.PingTimeout();
        Destroy();
    }
}

/*
 * async operation
 *
 */

void
PingClient::Cancel()
{
    event.Delete();
    Destroy();
}


/*
 * constructor
 *
 */

bool
ping_available(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

void
ping(EventLoop &event_loop, struct pool &pool, SocketAddress address,
     PingClientHandler &handler,
     CancellablePointer &cancel_ptr)
{
    UniqueSocketDescriptor fd;
    if (!fd.CreateNonBlock(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) {
        handler.PingError(std::make_exception_ptr(MakeErrno("Failed to create ping socket")));
        return;
    }

    const IPv4Address bind_address(0);

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    socklen_t sin_length = sizeof(sin);
    if (!fd.Bind(bind_address) ||
        getsockname(fd.Get(), (struct sockaddr *)&sin, &sin_length) < 0) {
        handler.PingError(std::make_exception_ptr(MakeErrno()));
        return;
    }

    uint16_t ident = sin.sin_port;

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

    struct iovec iov = {
        .iov_base = &packet,
        .iov_len = sizeof(packet),
    };

    struct msghdr m = {
        .msg_name = deconst_address(address.GetAddress()),
        .msg_namelen = socklen_t(address.GetSize()),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    ssize_t nbytes = sendmsg(fd.Get(), &m, 0);
    if (nbytes < 0) {
        handler.PingError(std::make_exception_ptr(MakeErrno()));
        return;
    }

    pool_ref(&pool);
    auto p = NewFromPool<PingClient>(pool, event_loop, pool,
                                     std::move(fd), ident,
                                     handler, cancel_ptr);
    p->ScheduleRead();
}
