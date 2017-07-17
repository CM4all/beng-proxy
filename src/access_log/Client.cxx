/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Datagram.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <errno.h>

void
LogClient::AppendString(enum beng_log_attribute attribute, const char *value)
{
    assert(value != nullptr);

    AppendAttribute(attribute, value, strlen(value) + 1);
}

bool
LogClient::Commit()
{
    assert(fd.IsDefined());
    assert(position > 0);

    if (position > sizeof(buffer))
        /* datagram is too large */
        return false;

    ssize_t nbytes = send(fd.Get(), buffer, position,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            /* silently ignore EAGAIN */
            return true;

        logger(1, "Failed to send to logger: ", strerror(errno));
        return false;
    }

    if ((size_t)nbytes != position)
        logger(1, "Short send to logger");

    return true;
}

bool
LogClient::Send(const AccessLogDatagram &d)
{
    Begin();

    if (d.valid_timestamp)
        AppendU64(LOG_TIMESTAMP, d.timestamp);

    if (d.remote_host != nullptr)
        AppendString(LOG_REMOTE_HOST, d.remote_host);

    if (d.host != nullptr)
        AppendString(LOG_HOST, d.host);

    if (d.site != nullptr)
        AppendString(LOG_SITE, d.site);

    if (d.valid_http_method)
        AppendU8(LOG_HTTP_METHOD, d.http_method);

    if (d.http_uri != nullptr)
        AppendString(LOG_HTTP_URI, d.http_uri);

    if (d.http_referer != nullptr)
        AppendString(LOG_HTTP_REFERER, d.http_referer);

    if (d.user_agent != nullptr)
        AppendString(LOG_USER_AGENT, d.user_agent);

    if (d.valid_http_status)
        AppendU16(LOG_HTTP_STATUS, d.http_status);

    if (d.valid_length)
        AppendU64(LOG_LENGTH, d.length);

    if (d.valid_traffic) {
        struct {
            uint64_t received, sent;
        } traffic = {
            .received = ToBE64(d.traffic_received),
            .sent = ToBE64(d.traffic_sent),
        };

        AppendAttribute(LOG_TRAFFIC, &traffic, sizeof(traffic));
    }

    if (d.valid_duration)
        AppendU64(LOG_DURATION, d.duration);

    return Commit();
}
