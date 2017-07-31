/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FAILURE_HXX
#define BENG_PROXY_FAILURE_HXX

#include "util/Compiler.h"

#include <chrono>

class SocketAddress;

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
failure_init();

void
failure_deinit();

void
failure_set(SocketAddress address,
            enum failure_status status, std::chrono::seconds duration);

void
failure_add(SocketAddress address);

/**
 * Unset a failure status.
 *
 * @param status the status to be removed; #FAILURE_OK is a catch-all
 * status that matches everything
 */
void
failure_unset(SocketAddress address, enum failure_status status);

gcc_pure
enum failure_status
failure_get_status(SocketAddress address);

#endif
