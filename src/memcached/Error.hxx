/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_ERROR_HXX
#define MEMCACHED_ERROR_HXX

#include <stdexcept>

class MemcachedClientError : public std::runtime_error {
public:
    explicit MemcachedClientError(const char *_msg)
        :std::runtime_error(_msg) {}
};

#endif
