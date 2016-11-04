/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_HANDLER_HXX
#define BENG_PROXY_TRANSLATE_HANDLER_HXX

#include <exception>

struct TranslateResponse;

struct TranslateHandler {
    void (*response)(TranslateResponse &response, void *ctx);
    void (*error)(std::exception_ptr ep, void *ctx);
};

#endif
