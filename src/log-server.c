/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"

#include <beng-proxy/log.h>

#include <glib.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

struct log_server {
    int fd;

    struct log_datagram datagram;

    char buffer[65536];
};

struct log_server *
log_server_new(int fd)
{
    struct log_server *server = g_new(struct log_server, 1);
    server->fd = fd;
    return server;
}

void
log_server_free(struct log_server *server)
{
    close(server->fd);
    g_free(server);
}

static const void *
read_uint8(uint8_t *value_r, const void *p, const uint8_t *end)
{
    const uint8_t *src = p;
    if (src + sizeof(*value_r) > end)
        return NULL;

    *value_r = *src++;
    return src;
}

static const void *
read_uint16(uint16_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return NULL;

    const uint16_t *src = p;
    uint16_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = GUINT16_FROM_BE(value);
    return src + 1;
}

static const void *
read_uint64(uint64_t *value_r, const void *p, const uint8_t *end)
{
    if ((const uint8_t *)p + sizeof(*value_r) > end)
        return NULL;

    const uint64_t *src = p;
    uint64_t value;
    memcpy(&value, src, sizeof(value));

    *value_r = GUINT64_FROM_BE(value);
    return src + 1;
}

static const void *
read_string(const char **value_r, const void *p, const uint8_t *end)
{
    const char *q = p;

    *value_r = q;

    q += strlen(q) + 1;
    return q > (const char *)end ? NULL : q;
}

static bool
log_server_apply_attributes(struct log_datagram *datagram, const void *p,
                            const uint8_t *end)
{
    assert(datagram != NULL);
    assert(p != NULL);
    assert(end != NULL);
    assert((const char *)p < (const char *)end);

    while (true) {
        const uint8_t *attr_p = p;
        if (attr_p >= end)
            return true;

        enum beng_log_attribute attr = *attr_p++;
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
            if (p == NULL)
                return false;

            datagram->http_method = u8;
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
            if (p == NULL)
                return false;

            datagram->http_status = u16;
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
            if (p != NULL)
                p = read_uint64(&datagram->traffic_sent, p, end);
            datagram->valid_traffic = true;
            break;
        }

        if (p == NULL)
            return false;
    }
}

static bool
log_server_apply_datagram(struct log_datagram *datagram, const void *p,
                          const void *end)
{
    const uint32_t *magic = p;
    if (*magic != log_magic)
        return false;

    return log_server_apply_attributes(datagram, magic + 1, end);
}

const struct log_datagram *
log_server_receive(struct log_server *server)
{
    ssize_t nbytes = recv(server->fd, server->buffer,
                          sizeof(server->buffer) - 1, 0);
    if (nbytes <= 0)
        return NULL;

    /* force null termination so we can use string functions inside
       the buffer */
    server->buffer[nbytes] = 0;

    memset(&server->datagram, 0, sizeof(server->datagram));

    return log_server_apply_datagram(&server->datagram, server->buffer,
                                     server->buffer + nbytes)
        ? &server->datagram
        : NULL;
}

