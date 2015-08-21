/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_PROTOCOL_HXX
#define BENG_DELEGATE_PROTOCOL_HXX

#include <stdint.h>

enum DelegateRequestCommand {
    /**
     * Open a regular file, and return the file descriptor in a
     * DELEGATE_FD packet.
     */
    DELEGATE_OPEN,
};

enum DelegateResponseCommand {
    /**
     * A file was successfully opened, and the file descriptor is in
     * the ancillary message.
     */
    DELEGATE_FD,

    /**
     * The operation has failed.  The payload contains the "errno"
     * value as an "int".
     */
    DELEGATE_ERRNO,
};

struct DelegateHeader {
    uint16_t length;
    uint16_t command;
};

struct DelegateIntPacket {
    DelegateHeader header;
    int value;
};

#endif
