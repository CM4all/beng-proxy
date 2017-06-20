/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GException.hxx"
#include "HttpMessageResponse.hxx"
#include "http_quark.h"
#include "gerrno.h"
#include "system/Error.hxx"
#include "util/Exception.hxx"

GError *
ToGError(std::exception_ptr ep)
{
    const auto msg = GetFullMessage(ep);

    try {
        FindRetrowNested<HttpMessageResponse>(ep);
    } catch (const HttpMessageResponse &e) {
        return g_error_new_literal(http_response_quark(), e.GetStatus(),
                                   msg.c_str());
    }

    try {
        FindRetrowNested<std::system_error>(ep);
    } catch (const std::system_error &e) {
        if (e.code().category() == ErrnoCategory())
            return g_error_new_literal(errno_quark(), e.code().value(),
                                       msg.c_str());
    }

    return g_error_new_literal(exception_quark(), 0, msg.c_str());
}

#if CLANG_OR_GCC_VERSION(4,0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#endif

void
ThrowGError(const GError &error)
{
    if (error.domain == http_response_quark())
        throw HttpMessageResponse(http_status_t(error.code), error.message);
    else if (error.domain == errno_quark())
        throw MakeErrno(error.code, error.message);
    else
        throw std::runtime_error(error.message);
}

#if CLANG_OR_GCC_VERSION(4,0)
#pragma GCC diagnostic pop
#endif

std::exception_ptr
ToException(const GError &error)
{
    try {
        ThrowGError(error);
        gcc_unreachable();
    } catch (...) {
        return std::current_exception();
    }
}
