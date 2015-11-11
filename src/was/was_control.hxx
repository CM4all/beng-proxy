/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CONTROL_HXX
#define BENG_PROXY_WAS_CONTROL_HXX

#include "glibfwd.hxx"

#include <was/protocol.h>

#include <stddef.h>

struct pool;
struct strmap;
template<typename T> struct ConstBuffer;
struct WasControl;

class WasControlHandler {
public:
    /**
     * A packet was received.
     *
     * @return false if the object was closed
     */
    virtual bool OnWasControlPacket(enum was_command cmd,
                                    ConstBuffer<void> payload) = 0;

    /**
     * Called after a group of control packets have been handled, and
     * the input buffer is drained.
     *
     * @return false if the #WasControl object has been destructed
     */
    virtual bool OnWasControlDrained() {
        return true;
    }

    virtual void OnWasControlDone() = 0;
    virtual void OnWasControlError(GError *error) = 0;
};

WasControl *
was_control_new(struct pool *pool, int fd, WasControlHandler &handler);

bool
was_control_free(WasControl *control);

bool
was_control_send(WasControl *control, enum was_command cmd,
                 const void *payload, size_t payload_length);

static inline bool
was_control_send_empty(WasControl *control, enum was_command cmd)
{
    return was_control_send(control, cmd, nullptr, 0);
}

bool
was_control_send_string(WasControl *control, enum was_command cmd,
                        const char *payload);

static inline bool
was_control_send_uint64(WasControl *control, enum was_command cmd,
                        uint64_t payload)
{
    return was_control_send(control, cmd, &payload, sizeof(payload));
}

bool
was_control_send_array(WasControl *control, enum was_command cmd,
                       ConstBuffer<const char *> values);

bool
was_control_send_strmap(WasControl *control, enum was_command cmd,
                        const struct strmap *map);

/**
 * Enables bulk mode.
 */
void
was_control_bulk_on(WasControl *control);

/**
 * Disables bulk mode and flushes the output buffer.
 */
bool
was_control_bulk_off(WasControl *control);

void
was_control_done(WasControl *control);

bool
was_control_is_empty(WasControl *control);

#endif
