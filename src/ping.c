/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ping.h"
#include "pool.h"
#include "pevent.h"
#include "address-envelope.h"
#include "async.h"

#include <event.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>

struct ping {
    struct pool *pool;

    int fd;

    uint16_t ident;

    struct event event;

    const struct ping_handler *handler;
    void *handler_ctx;
    struct async_operation async_operation;
};

static const struct timeval ping_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

static void
ping_schedule_read(struct ping *p)
{
    p_event_add(&p->event, &ping_timeout, p->pool, "ping");
}

static u_short
in_cksum(const u_short *addr, register int len, u_short csum)
{
	register int nleft = len;
	const u_short *w = addr;
	register u_short answer;
	register int sum = csum;

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
deconst_address(const struct address_envelope *envelope)
{
    union {
        const struct sockaddr *in;
        void *out;
    } u = { .in = &envelope->address };
    return u.out;
}

static bool
parse_reply(struct msghdr *msg, size_t cc, uint16_t ident)
{
    const uint8_t *buf = msg->msg_iov->iov_base;
    const struct icmphdr *icp = (const struct icmphdr *)buf;
    if (cc < sizeof(*icp))
        return false;

    return icp->type == ICMP_ECHOREPLY && icp->un.echo.id == ident;
}

static void
ping_read(struct ping *p)
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

    int cc = recvmsg(p->fd, &msg, MSG_DONTWAIT);
    if (cc >= 0) {
        if (parse_reply(&msg, cc, p->ident)) {
            async_operation_finished(&p->async_operation);

            close(p->fd);
            p->handler->response(p->handler_ctx);
            pool_unref(p->pool);
        } else
            ping_schedule_read(p);
    } else if (errno == EAGAIN || errno == EINTR) {
        ping_schedule_read(p);
    } else {
        async_operation_finished(&p->async_operation);

        GError *error = g_error_new_literal(ping_quark(), errno,
                                            strerror(errno));
        close(p->fd);
        p->handler->error(error, p->handler_ctx);
        pool_unref(p->pool);
    }
}


/*
 * libevent callback
 *
 */

static void
ping_event_callback(int fd G_GNUC_UNUSED, short event, void *ctx)
{
    struct ping *p = ctx;

    assert(p->fd >= 0);

    p_event_consumed(&p->event, p->pool);

    if (event & EV_READ) {
        ping_read(p);
    } else {
        close(p->fd);
        p->handler->timeout(p->handler_ctx);
        pool_unref(p->pool);
    }

    pool_commit();
}

/*
 * async operation
 *
 */

static struct ping *
async_to_ping(struct async_operation *ao)
{
    return (struct ping *)(((char*)ao) - offsetof(struct ping, async_operation));
}

static void
ping_request_abort(struct async_operation *ao)
{
    struct ping *p = async_to_ping(ao);

    event_del(&p->event);
    close(p->fd);
    pool_unref(p->pool);
}

static const struct async_operation_class ping_async_operation = {
    .abort = ping_request_abort,
};


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
ping(struct pool *pool, const struct address_envelope *envelope,
     const struct ping_handler *handler, void *ctx,
     struct async_operation_ref *async_ref)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        GError *error = g_error_new(ping_quark(), errno,
                                    "Failed to create ping socket: %s",
                                    strerror(errno));
        handler->error(error, ctx);
        return;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    socklen_t sin_length = sizeof(sin);
    if (bind(fd, (const struct sockaddr *)&sin, sin_length) < 0 ||
        getsockname(fd, (struct sockaddr *)&sin, &sin_length) < 0) {
        GError *error = g_error_new_literal(ping_quark(), errno,
                                            strerror(errno));
        handler->error(error, ctx);
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
        .msg_name = deconst_address(envelope),
        .msg_namelen = envelope->length,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    ssize_t nbytes = sendmsg(fd, &m, 0);
    if (nbytes < 0) {
        GError *error = g_error_new_literal(ping_quark(), errno,
                                            strerror(errno));
        close(fd);
        handler->error(error, ctx);
        return;
    }

    pool_ref(pool);
    struct ping *p = p_malloc(pool, sizeof(*p));
    p->pool = pool;
    p->fd = fd;
    p->ident = ident;
    p->handler = handler;
    p->handler_ctx = ctx;

    event_set(&p->event, fd, EV_READ|EV_TIMEOUT, ping_event_callback, p);
    ping_schedule_read(p);

    async_init(&p->async_operation, &ping_async_operation);
    async_ref_set(async_ref, &p->async_operation);
}
