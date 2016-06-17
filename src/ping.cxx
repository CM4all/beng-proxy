/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ping.hxx"
#include "pool.hxx"
#include "async.hxx"
#include "net/SocketAddress.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "event/Duration.hxx"
#include "gerrno.h"
#include "util/Cast.hxx"

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>

class PingClient {
    struct pool &pool;

    const int fd;

    const uint16_t ident;

    Event event;

    PingClientHandler &handler;
    struct async_operation async_operation;

public:
    PingClient(struct pool &_pool, int _fd, uint16_t _ident,
               PingClientHandler &_handler,
               struct async_operation_ref &async_ref)
        :pool(_pool), fd(_fd), ident(_ident),
         event(fd, EV_READ|EV_TIMEOUT,
               MakeEventCallback(PingClient, EventCallback), this),
         handler(_handler) {
        async_operation.Init2<PingClient, &PingClient::async_operation,
                              &PingClient::Abort>();
        async_ref.Set(async_operation);
    }

    void ScheduleRead() {
        event.Add(EventDuration<10>::value);
    }

private:
    void EventCallback(evutil_socket_t fd, short events);

    void Read();

    void Abort();
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

    int cc = recvmsg(fd, &msg, MSG_DONTWAIT);
    if (cc >= 0) {
        if (parse_reply(&msg, cc, ident)) {
            async_operation.Finished();

            close(fd);
            handler.PingResponse();
            pool_unref(&pool);
        } else
            ScheduleRead();
    } else if (errno == EAGAIN || errno == EINTR) {
        ScheduleRead();
    } else {
        async_operation.Finished();

        GError *error = new_error_errno();
        close(fd);
        handler.PingError(error);
        pool_unref(&pool);
    }
}


/*
 * libevent callback
 *
 */

inline void
PingClient::EventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(fd >= 0);

    if (events & EV_READ) {
        Read();
    } else {
        close(fd);
        handler.PingTimeout();
        pool_unref(&pool);
    }

    pool_commit();
}

/*
 * async operation
 *
 */

inline void
PingClient::Abort()
{
    event.Delete();
    close(fd);
    pool_unref(&pool);
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
ping(struct pool &pool, SocketAddress address,
     PingClientHandler &handler,
     struct async_operation_ref &async_ref)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        GError *error = new_error_errno_msg("Failed to create ping socket");
        handler.PingError(error);
        return;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    socklen_t sin_length = sizeof(sin);
    if (bind(fd, (const struct sockaddr *)&sin, sin_length) < 0 ||
        getsockname(fd, (struct sockaddr *)&sin, &sin_length) < 0) {
        GError *error = new_error_errno();
        handler.PingError(error);
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

    ssize_t nbytes = sendmsg(fd, &m, 0);
    if (nbytes < 0) {
        GError *error = new_error_errno();
        close(fd);
        handler.PingError(error);
        return;
    }

    pool_ref(&pool);
    auto p = NewFromPool<PingClient>(pool, pool, fd, ident,
                                     handler, async_ref);
    p->ScheduleRead();
}
