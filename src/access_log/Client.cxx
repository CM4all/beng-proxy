/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"

#include <daemon/log.h>

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

        daemon_log(1, "Failed to send to logger: %s\n", strerror(errno));
        return false;
    }

    if ((size_t)nbytes != position)
        daemon_log(1, "Short send to logger: %s\n", strerror(errno));

    return true;
}
