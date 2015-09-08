/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_HANDLER_HXX
#define BENG_DELEGATE_HANDLER_HXX

#include "glibfwd.hxx"

struct delegate_handler {
    void (*success)(int fd, void *ctx);
    void (*error)(GError *error, void *ctx);
};

#endif
