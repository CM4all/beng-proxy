/*
 * A client for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_CLIENT_HXX
#define BENG_PROXY_LOG_CLIENT_HXX

#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/ByteOrder.hxx"

#include <beng-proxy/log.h>

#include <string.h>

class LogClient {
    const Logger logger;

    UniqueSocketDescriptor fd;

    size_t position;
    char buffer[32768];

public:
    explicit LogClient(UniqueSocketDescriptor &&_fd)
        :logger("access_log"), fd(std::move(_fd)) {}

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

    void AppendU8(enum beng_log_attribute attribute, uint8_t value) {
        AppendAttribute(attribute, &value, sizeof(value));
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

#endif
