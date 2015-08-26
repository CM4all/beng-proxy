/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_HANDLER_HXX
#define BENG_PROXY_TRANSLATE_HANDLER_HXX

#include "glibfwd.hxx"

struct TranslateResponse;

struct TranslateHandler {
    void (*response)(TranslateResponse &response, void *ctx);
    void (*error)(GError *error, void *ctx);
};

#endif
