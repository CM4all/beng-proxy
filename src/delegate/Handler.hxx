/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_HANDLER_HXX
#define BENG_DELEGATE_HANDLER_HXX

#include <exception>

class DelegateHandler {
public:
    virtual void OnDelegateSuccess(int fd) = 0;
    virtual void OnDelegateError(std::exception_ptr ep) = 0;
};

#endif
