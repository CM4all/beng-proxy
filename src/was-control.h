/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CONTROL_H
#define BENG_PROXY_WAS_CONTROL_H

#include <was/protocol.h>

#include <glib.h>

#include <stddef.h>
#include <stdbool.h>

struct pool;
struct strmap;

struct was_control_handler {
    /**
     * A packet was received.
     *
     * @return false if the object was closed
     */
    bool (*packet)(enum was_command cmd, const void *payload,
                   size_t payload_length, void *ctx);

    /**
     * Called after a group of control packets have been handled, and
     * the input buffer is drained.
     *
     * @return false if the #was_control object has been destructed
     */
    bool (*drained)(void *ctx);

    void (*eof)(void *ctx);
    void (*abort)(GError *error, void *ctx);
};

struct was_control *
was_control_new(struct pool *pool, int fd,
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
was_control_send_array(struct was_control *control, enum was_command cmd,
                       const char *const values[], unsigned num_values);

bool
was_control_send_strmap(struct was_control *control, enum was_command cmd,
                        struct strmap *map);

/**
 * Enables bulk mode.
 */
void
was_control_bulk_on(struct was_control *control);

/**
 * Disables bulk mode and flushes the output buffer.
 */
bool
was_control_bulk_off(struct was_control *control);

void
was_control_done(struct was_control *control);

bool
was_control_is_empty(struct was_control *control);

#endif
