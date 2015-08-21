/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_PROTOCOL_HXX
#define BENG_DELEGATE_PROTOCOL_HXX

#include <stdint.h>

enum class DelegateRequestCommand : uint16_t {
    /**
     * Open a regular file, and return the file descriptor in a
     * #DelegateResponseCommand::FD packet.
     */
    OPEN,
};

enum class DelegateResponseCommand : uint16_t {
    /**
     * A file was successfully opened, and the file descriptor is in
     * the ancillary message.
     */
    FD,

    /**
     * The operation has failed.  The payload contains the "errno"
     * value as an "int".
     */
    ERRNO,
};

struct DelegateRequestHeader {
    uint16_t length;
    DelegateRequestCommand command;
};

struct DelegateResponseHeader {
    uint16_t length;
    DelegateResponseCommand command;
};

struct DelegateIntPacket {
    DelegateResponseHeader header;
    int value;
};

#endif
