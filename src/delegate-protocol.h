/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_PROTOCOL_H
#define BENG_DELEGATE_PROTOCOL_H

#include <stdint.h>

struct delegate_header {
    uint16_t length;
    uint16_t command;
};

#endif
