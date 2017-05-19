/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GException.hxx"
#include "HttpMessageResponse.hxx"
#include "http_quark.h"
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
    return g_error_new_literal(exception_quark(), 0, e.what());
}

GError *
ToGError(std::exception_ptr ep)
{
    try {
        FindRetrowNested<HttpMessageResponse>(ep);
    } catch (const HttpMessageResponse &e) {
        return g_error_new_literal(http_response_quark(), e.GetStatus(),
                                   e.what());
    }

    try {
        std::rethrow_exception(ep);
    } catch (const std::exception &e) {
        return ToGError(e);
    } catch (...) {
        return g_error_new_literal(exception_quark(), 0,
                                   "Unexpected C++ exception");
    }
}
