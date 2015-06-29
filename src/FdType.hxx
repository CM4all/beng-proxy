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

enum {
    FD_ANY = (FD_FILE | FD_PIPE | FD_SOCKET | FD_TCP),
    FD_ANY_SOCKET = (FD_SOCKET | FD_TCP),
};

typedef unsigned FdTypeMask;

#endif
