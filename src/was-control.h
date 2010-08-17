/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CONTROL_H
#define BENG_PROXY_WAS_CONTROL_H

#include "pool.h"

#include <was/protocol.h>

#include <stddef.h>
#include <stdbool.h>

struct strmap;

struct was_control_handler {
    /**
     * A packet was received.
     *
     * @return false if the object was closed
     */
    bool (*packet)(enum was_command cmd, const void *payload,
                   size_t payload_length, void *ctx);

    void (*eof)(void *ctx);
    void (*abort)(void *ctx);
};

struct was_control *
was_control_new(pool_t pool, int fd,
                const struct was_control_handler *handler,
                void *handler_ctx);

bool
was_control_free(struct was_control *control);

bool
was_control_send(struct was_control *control, enum was_command cmd,
                 const void *payload, size_t payload_length);

static inline bool
was_control_send_empty(struct was_control *control, enum was_command cmd)
{
    return was_control_send(control, cmd, NULL, 0);
}

bool
was_control_send_string(struct was_control *control, enum was_command cmd,
                        const char *payload);

static inline bool
was_control_send_uint64(struct was_control *control, enum was_command cmd,
                        uint64_t payload)
{
    return was_control_send(control, cmd, &payload, sizeof(payload));
}

bool
was_control_send_strmap(struct was_control *control, enum was_command cmd,
                        struct strmap *map);

void
was_control_done(struct was_control *control);

#endif
