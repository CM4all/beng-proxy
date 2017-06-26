/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef FCGI_ERROR_HXX
#define FCGI_ERROR_HXX

#include <stdexcept>

class FcgiClientError : public std::runtime_error {
public:
    explicit FcgiClientError(const char *_msg)
        :std::runtime_error(_msg) {}
};

#endif
