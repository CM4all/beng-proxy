/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FD_TYPE_HXX
#define BENG_PROXY_FD_TYPE_HXX

enum FdType {
    /**
     * No file descriptor available.  Special value that is only
     * supported by few libraries.
     */
    FD_NONE = 00,

    FD_FILE = 01,
    FD_PIPE = 02,
    FD_SOCKET = 04,
    FD_TCP = 010,

    /** a character device, such as /dev/zero or /dev/null */
    FD_CHARDEV = 020,
};

typedef unsigned FdTypeMask;

static constexpr FdTypeMask FD_ANY_SOCKET = FD_SOCKET | FD_TCP;
static constexpr FdTypeMask FD_ANY = FD_FILE | FD_PIPE | FD_ANY_SOCKET;

static constexpr inline bool
IsAnySocket(FdType type)
{
    return (FdTypeMask(type) & FD_ANY_SOCKET) != 0;
}

#endif
