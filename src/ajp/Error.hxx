/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AJP_ERROR_HXX
#define AJP_ERROR_HXX

#include <stdexcept>

class AjpClientError : public std::runtime_error {
public:
    explicit AjpClientError(const char *_msg)
        :std::runtime_error(_msg) {}
};

#endif
