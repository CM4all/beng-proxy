/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_STRUCT_HXX
#define ISTREAM_STRUCT_HXX

/**
 * Obsolete.  Use class #Istream instead.
 */
struct istream {
    istream() = default;

    istream(const struct istream &) = delete;
    const istream &operator=(const struct istream &) = delete;
};

#endif
