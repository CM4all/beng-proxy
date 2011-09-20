/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DIRECT_H
#define BENG_PROXY_ISTREAM_DIRECT_H

enum istream_direct {
    ISTREAM_FILE = 01,
    ISTREAM_PIPE = 02,
    ISTREAM_SOCKET = 04,
    ISTREAM_TCP = 010,

    /** a character device, such as /dev/zero or /dev/null */
    ISTREAM_CHARDEV = 020,
};

enum {
    ISTREAM_ANY = (ISTREAM_FILE | ISTREAM_PIPE | ISTREAM_SOCKET | ISTREAM_TCP),
};

typedef unsigned istream_direct_t;

#endif
