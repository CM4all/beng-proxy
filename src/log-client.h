/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_CLIENT_H
#define BENG_PROXY_LOG_CLIENT_H

#include <beng-proxy/log.h>

#include <stddef.h>
#include <stdbool.h>

struct log_client *
log_client_new(int fd);

void
log_client_free(struct log_client *l);

void
log_client_begin(struct log_client *client);

void
log_client_append_attribute(struct log_client *client,
                            enum beng_log_attribute attribute,
                            const void *value, size_t length);

static inline void
log_client_append_u8(struct log_client *client,
                     enum beng_log_attribute attribute, uint8_t value)
{
    log_client_append_attribute(client, attribute, &value, sizeof(value));
}

void
log_client_append_u16(struct log_client *client,
                      enum beng_log_attribute attribute, uint16_t value);

void
log_client_append_u64(struct log_client *client,
                      enum beng_log_attribute attribute, uint64_t value);

void
log_client_append_string(struct log_client *client,
                         enum beng_log_attribute attribute, const char *value);

bool
log_client_commit(struct log_client *client);

#endif
