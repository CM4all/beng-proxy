/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "Datagram.hxx"
#include "util/ByteOrder.hxx"

#include <beng-proxy/log.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

struct log_server {
    const int fd;

    AccessLogDatagram datagram;

    char buffer[65536];

public:
    log_server(int _fd):fd(_fd) {}

    ~log_server() {
        close(fd);
    }
};

struct log_server *
log_server_new(int fd)
{
    return new log_server(fd);
}

void
log_server_free(struct log_server *server)
{
    delete server;
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

const AccessLogDatagram *
log_server_receive(struct log_server *server)
{
    while (true) {
        ssize_t nbytes = recv(server->fd, server->buffer,
                              sizeof(server->buffer) - 1, 0);
        if (nbytes <= 0) {
            if (nbytes < 0 && errno == EAGAIN)
                continue;
            return nullptr;
        }

        /* force null termination so we can use string functions inside
           the buffer */
        server->buffer[nbytes] = 0;

        memset(&server->datagram, 0, sizeof(server->datagram));

        if (log_server_apply_datagram(&server->datagram, server->buffer,
                                      server->buffer + nbytes))
            return &server->datagram;
    }
}

