/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CGI_ERROR_HXX
#define CGI_ERROR_HXX

#include <stdexcept>

class CgiError : public std::runtime_error {
public:
    explicit CgiError(const char *_msg)
        :std::runtime_error(_msg) {}
};

#endif
