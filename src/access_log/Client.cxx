/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/ByteOrder.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

struct LogClient {
    UniqueSocketDescriptor fd;

    size_t position;
    char buffer[32768];

    explicit LogClient(UniqueSocketDescriptor &&_fd)
        :fd(std::move(_fd)) {}

    void Begin() {
        position = 0;
        Append(&log_magic, sizeof(log_magic));
    }

    void Append(const void *p, size_t size) {
        if (position + size <= sizeof(buffer))
            memcpy(buffer + position, p, size);

        position += size;
    }

    void AppendAttribute(enum beng_log_attribute attribute,
                         const void *value, size_t size) {
        const uint8_t attribute8 = (uint8_t)attribute;
        Append(&attribute8, sizeof(attribute8));
        Append(value, size);
    }

    void AppendU16(enum beng_log_attribute attribute, uint16_t value) {
        const uint16_t value2 = ToBE16(value);
        AppendAttribute(attribute, &value2, sizeof(value2));
    }

    void AppendU64(enum beng_log_attribute attribute, uint64_t value) {
        const uint64_t value2 = ToBE64(value);
        AppendAttribute(attribute, &value2, sizeof(value2));
    }

    void AppendString(enum beng_log_attribute attribute, const char *value);

    bool Commit();
};

LogClient *
log_client_new(UniqueSocketDescriptor &&fd)
{
    assert(fd.IsDefined());

    return new LogClient(std::move(fd));
}

void
log_client_free(LogClient *l)
{
    delete l;
}

void
log_client_begin(LogClient *client)
{
    client->Begin();
}

void
log_client_append_attribute(LogClient *client,
                            enum beng_log_attribute attribute,
                            const void *value, size_t length)
{
    client->AppendAttribute(attribute, value, length);
}

void
log_client_append_u16(LogClient *client,
                      enum beng_log_attribute attribute, uint16_t value)
{
    client->AppendU16(attribute, value);
}

void
log_client_append_u64(LogClient *client,
                      enum beng_log_attribute attribute, uint64_t value)
{
    client->AppendU64(attribute, value);
}

void
LogClient::AppendString(enum beng_log_attribute attribute, const char *value)
{
    assert(value != nullptr);

    AppendAttribute(attribute, value, strlen(value) + 1);
}

void
log_client_append_string(LogClient *client,
                         enum beng_log_attribute attribute, const char *value)
{
    client->AppendString(attribute, value);
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

bool
log_client_commit(LogClient *client)
{
    assert(client != nullptr);

    return client->Commit();
}
