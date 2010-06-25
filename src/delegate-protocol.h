/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_PROTOCOL_H
#define BENG_DELEGATE_PROTOCOL_H

#include <stdint.h>

enum delegate_request_command {
    /**
     * Open a regular file, and return the file descriptor in a
     * #DELEGATE_FD packet.
     */
    DELEGATE_OPEN,
};

enum delegate_response_command {
    /**
     * A file was successfully opened, and the file descriptor is in
     * the ancillary message.
     */
    DELEGATE_FD,
};

struct delegate_header {
    uint16_t length;
    uint16_t command;
};

#endif
