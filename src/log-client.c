/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-client.h"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

struct log_client {
    int fd;

    size_t position;
    char buffer[32768];
};

struct log_client *
log_client_new(int fd)
{
    assert(fd >= 0);

    struct log_client *l = g_new(struct log_client, 1);
    l->fd = fd;
    return l;
}

void
log_client_free(struct log_client *l)
{
    close(l->fd);
    g_free(l);
}

static void
log_client_append(struct log_client *client,
                  const void *p, size_t length)
{
    if (client->position + length <= sizeof(client->buffer))
        memcpy(client->buffer + client->position, p, length);

    client->position += length;
}

void
log_client_begin(struct log_client *client)
{
    client->position = 0;

    log_client_append(client, &log_magic, sizeof(log_magic));
}

void
log_client_append_attribute(struct log_client *client,
                            enum beng_log_attribute attribute,
                            const void *value, size_t length)
{
    uint8_t attribute8 = (uint8_t)attribute;
    log_client_append(client, &attribute8, sizeof(attribute8));
    log_client_append(client, value, length);
}

void
log_client_append_u16(struct log_client *client,
                      enum beng_log_attribute attribute, uint16_t value)
{
    const uint16_t value2 = GUINT16_TO_BE(value);
    log_client_append_attribute(client, attribute, &value2, sizeof(value2));
}

void
log_client_append_u64(struct log_client *client,
                      enum beng_log_attribute attribute, uint64_t value)
{
    const uint64_t value2 = GUINT64_TO_BE(value);
    log_client_append_attribute(client, attribute, &value2, sizeof(value2));
}

void
log_client_append_string(struct log_client *client,
                         enum beng_log_attribute attribute, const char *value)
{
    assert(value != NULL);

    log_client_append_attribute(client, attribute, value, strlen(value) + 1);
}

bool
log_client_commit(struct log_client *client)
{
    assert(client != NULL);
    assert(client->fd >= 0);
    assert(client->position > 0);

    if (client->position > sizeof(client->buffer))
        /* datagram is too large */
        return false;

    ssize_t nbytes = send(client->fd, client->buffer, client->position,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            /* silently ignore EAGAIN */
            return true;

        daemon_log(1, "Failed to send to logger: %s\n", strerror(errno));
        return false;
    }

    if ((size_t)nbytes != client->position)
        daemon_log(1, "Short send to logger: %s\n", strerror(errno));

    return true;
}
