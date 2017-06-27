/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef WAS_ERROR_HXX
#define WAS_ERROR_HXX

#include <stdexcept>

class WasError : public std::runtime_error {
public:
    explicit WasError(const char *_msg)
        :std::runtime_error(_msg) {}
};

class WasProtocolError : public WasError {
public:
    explicit WasProtocolError(const char *_msg)
        :WasError(_msg) {}
};

#endif
