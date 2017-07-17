/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "util/ByteOrder.hxx"

#include <beng-proxy/log.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

AccessLogServer::~AccessLogServer()
{
    close(fd);
}

static const void *
read_uint8(uint8_t *value_r, const void *p, const uint8_t *end)
{
    auto src = (const uint8_t *)p;
    if (src + sizeof(*value_r) > end)
        return nullptr;

    *value_r = *src++;
    return src;
}

static const void *
read_uint16(uint16_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return nullptr;

    auto src = (const uint16_t *)p;
    uint16_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = FromBE16(value);
    return src + 1;
}

static const void *
read_uint64(uint64_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return nullptr;

    auto src = (const uint64_t *)p;
    uint64_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = FromBE64(value);
    return src + 1;
}

static const void *
read_string(const char **value_r, const void *p, const uint8_t *end)
{
    auto q = (const char *)p;

    *value_r = q;

    q += strlen(q) + 1;
    return q > (const char *)end ? nullptr : q;
}

static bool
log_server_apply_attributes(AccessLogDatagram *datagram, const void *p,
                            const uint8_t *end)
{
    assert(datagram != nullptr);
    assert(p != nullptr);
    assert(end != nullptr);
    assert((const char *)p < (const char *)end);

    while (true) {
        auto attr_p = (const uint8_t *)p;
        if (attr_p >= end)
            return true;

        auto attr = (enum beng_log_attribute)*attr_p++;
        p = attr_p;

        switch (attr) {
            uint8_t u8;
            uint16_t u16;

        case LOG_NULL:
            break;

        case LOG_TIMESTAMP:
            p = read_uint64(&datagram->timestamp, p, end);
            datagram->valid_timestamp = true;
            break;

        case LOG_REMOTE_HOST:
            p = read_string(&datagram->remote_host, p, end);
            break;

        case LOG_HOST:
            p = read_string(&datagram->host, p, end);
            break;

        case LOG_SITE:
            p = read_string(&datagram->site, p, end);
            break;

        case LOG_HTTP_METHOD:
            p = read_uint8(&u8, p, end);
            if (p == nullptr)
                return false;

            datagram->http_method = http_method_t(u8);
            if (!http_method_is_valid(datagram->http_method))
                return false;

            datagram->valid_http_method = true;
            break;

        case LOG_HTTP_URI:
            p = read_string(&datagram->http_uri, p, end);
            break;

        case LOG_HTTP_REFERER:
            p = read_string(&datagram->http_referer, p, end);
            break;

        case LOG_USER_AGENT:
            p = read_string(&datagram->user_agent, p, end);
            break;

        case LOG_HTTP_STATUS:
            p = read_uint16(&u16, p, end);
            if (p == nullptr)
                return false;

            datagram->http_status = http_status_t(u16);
            if (!http_status_is_valid(datagram->http_status))
                return false;

            datagram->valid_http_status = true;
            break;

        case LOG_LENGTH:
            p = read_uint64(&datagram->length, p, end);
            datagram->valid_length = true;
            break;

        case LOG_TRAFFIC:
            p = read_uint64(&datagram->traffic_received, p, end);
            if (p != nullptr)
                p = read_uint64(&datagram->traffic_sent, p, end);
            datagram->valid_traffic = true;
            break;

        case LOG_DURATION:
            p = read_uint64(&datagram->duration, p, end);
            datagram->valid_duration = true;
            break;
        }

        if (p == nullptr)
            return false;
    }
}

static bool
log_server_apply_datagram(AccessLogDatagram *datagram, const void *p,
                          const void *end)
{
    auto magic = (const uint32_t *)p;
    if (*magic != log_magic)
        return false;

    return log_server_apply_attributes(datagram, magic + 1,
                                       (const uint8_t *)end);
}

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

    int n = recvmmsg(fd, &msgs.front(), msgs.size(),
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
        uint8_t *buffer = payloads[current_payload];
        size_t nbytes = sizes[current_payload];
        ++current_payload;

        /* force null termination so we can use string functions inside
           the buffer */
        buffer[nbytes] = 0;

        memset(&datagram, 0, sizeof(datagram));

        datagram.logger_client_address = address;

        if (log_server_apply_datagram(&datagram, buffer, buffer + nbytes))
            return &datagram;
    }
}

