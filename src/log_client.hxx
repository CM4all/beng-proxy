/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_CLIENT_HXX
#define BENG_PROXY_LOG_CLIENT_HXX

#include <beng-proxy/log.h>

#include <stddef.h>

struct LogClient;

LogClient *
log_client_new(int fd);

void
log_client_free(LogClient *l);

void
log_client_begin(LogClient *client);

void
log_client_append_attribute(LogClient *client,
                            enum beng_log_attribute attribute,
                            const void *value, size_t length);

static inline void
log_client_append_u8(LogClient *client,
                     enum beng_log_attribute attribute, uint8_t value)
{
    log_client_append_attribute(client, attribute, &value, sizeof(value));
}

void
log_client_append_u16(LogClient *client,
                      enum beng_log_attribute attribute, uint16_t value);

void
log_client_append_u64(LogClient *client,
                      enum beng_log_attribute attribute, uint64_t value);

void
log_client_append_string(LogClient *client,
                         enum beng_log_attribute attribute, const char *value);

bool
log_client_commit(LogClient *client);

#endif
