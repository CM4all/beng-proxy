/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GException.hxx"
#include "HttpMessageResponse.hxx"
#include "http_quark.h"
#include "gerrno.h"
#include "system/Error.hxx"
#include "util/Exception.hxx"

void
SetGError(GError **error_r, const std::exception &e)
{
    g_set_error_literal(error_r, exception_quark(), 0,
                        GetFullMessage(e).c_str());
}

GError *
ToGError(const std::exception &e)
{
    return g_error_new_literal(exception_quark(), 0,
                               GetFullMessage(e).c_str());
}

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
