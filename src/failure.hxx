/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FAILURE_HXX
#define BENG_PROXY_FAILURE_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct sockaddr;

enum failure_status {
    /**
     * No failure, host is ok.
     */
    FAILURE_OK,

    /**
     * Host is being faded out (graceful shutdown).  No new sessions.
     */
    FAILURE_FADE,

    /**
     * The response received from the server indicates a server error.
     */
    FAILURE_RESPONSE,

    /**
     * Host has failed.
     */
    FAILURE_FAILED,

    /**
     * The failure was submitted by a "monitor", and will not expire
     * until the monitor detects recovery.
     */
    FAILURE_MONITOR,
};

void
failure_init(struct pool *pool);

void
failure_deinit(void);

void
failure_set(const struct sockaddr *address, size_t length,
            enum failure_status status, unsigned duration);

static inline void
failure_add(const struct sockaddr *address, size_t length)
{
    failure_set(address, length, FAILURE_FAILED, 20);
}

/**
 * Unset a failure status.
 *
 * @param status the status to be removed; #FAILURE_OK is a catch-all
 * status that matches everything
 */
void
failure_unset(const struct sockaddr *address, size_t length,
              enum failure_status status);

gcc_pure
enum failure_status
failure_get_status(const struct sockaddr *address, size_t length);

#endif
