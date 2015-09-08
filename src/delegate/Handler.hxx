/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_HANDLER_HXX
#define BENG_DELEGATE_HANDLER_HXX

#include "glibfwd.hxx"

class DelegateHandler {
public:
    virtual void OnDelegateSuccess(int fd) = 0;
    virtual void OnDelegateError(GError *error) = 0;
};

#endif
