/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GException.hxx"
#include "HttpMessageResponse.hxx"
#include "http_quark.h"
#include "http_client.hxx"
#include "nfs/Error.hxx"
#include "nfs/Quark.hxx"
#include "was/Error.hxx"
#include "was/Quark.hxx"
#include "fcgi/Error.hxx"
#include "fcgi/Quark.hxx"
#include "memcached/Error.hxx"
#include "memcached/Quark.hxx"
#include "gerrno.h"
#include "widget/Error.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "util/ScopeExit.hxx"

/**
 * A GQuark for std::exception.
 */
G_GNUC_CONST
static inline GQuark
exception_quark()
{
    return g_quark_from_static_string("std::exception");
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

    try {
        FindRetrowNested<NfsClientError>(ep);
    } catch (const NfsClientError &e) {
        return g_error_new_literal(nfs_client_quark(), e.GetCode(), msg.c_str());
    }

    try {
        FindRetrowNested<HttpClientError>(ep);
    } catch (const HttpClientError &e) {
        return g_error_new_literal(http_client_quark(), int(e.GetCode()),
                                   msg.c_str());
    }

    try {
        FindRetrowNested<WasError>(ep);
    } catch (const WasError &e) {
        return g_error_new_literal(was_quark(), 0, msg.c_str());
    }

    try {
        FindRetrowNested<FcgiClientError>(ep);
    } catch (const FcgiClientError &e) {
        return g_error_new_literal(fcgi_quark(), 0, msg.c_str());
    }

    try {
        FindRetrowNested<MemcachedClientError>(ep);
    } catch (const MemcachedClientError &e) {
        return g_error_new_literal(memcached_client_quark(), 0, msg.c_str());
    }

    try {
        FindRetrowNested<WidgetError>(ep);
    } catch (const WidgetError &e) {
        return g_error_new_literal(widget_quark(), int(e.GetCode()),
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
    else if (error.domain == nfs_client_quark())
        throw NfsClientError(error.code, error.message);
    else if (error.domain == http_client_quark())
        throw HttpClientError(HttpClientErrorCode(error.code), error.message);
    else if (error.domain == was_quark())
        throw WasError(error.message);
    else if (error.domain == fcgi_quark())
        throw FcgiClientError(error.message);
    else if (error.domain == memcached_client_quark())
        throw MemcachedClientError(error.message);
    else if (error.domain == widget_quark())
        throw WidgetError(WidgetErrorCode(error.code), error.message);
    else
        throw std::runtime_error(error.message);
}

#if CLANG_OR_GCC_VERSION(4,0)
#pragma GCC diagnostic pop
#endif

void
ThrowFreeGError(GError *error)
{
    AtScopeExit(error) { g_error_free(error); };
    ThrowGError(*error);
}

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
