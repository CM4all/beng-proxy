/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Convert C++ exception to a HTTP response.
 */

#include "Request.hxx"
#include "Instance.hxx"
#include "http_client.hxx"
#include "nfs/Error.hxx"
#include "ajp/Error.hxx"
#include "memcached/Error.hxx"
#include "cgi/Error.hxx"
#include "fcgi/Error.hxx"
#include "was/Error.hxx"
#include "widget/Error.hxx"
#include "http_response.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http/MessageHttpResponse.hxx"
#include "HttpMessageResponse.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"

#include <nfsc/libnfs-raw-nfs.h>

static MessageHttpResponse
Dup(struct pool &pool, http_status_t status, const char *msg)
{
    return {status, p_strdup(&pool, msg)};
}

gcc_pure
static MessageHttpResponse
ToResponse(struct pool &pool, std::exception_ptr ep)
{
    try {
        FindRetrowNested<HttpMessageResponse>(ep);
    } catch (const HttpMessageResponse &e) {
        return Dup(pool, e.GetStatus(), e.what());
    }

    try {
        FindRetrowNested<std::system_error>(ep);
    } catch (const std::system_error &e) {
        if (e.code().category() == ErrnoCategory()) {
            switch (e.code().value()) {
            case ENOENT:
            case ENOTDIR:
                return {HTTP_STATUS_NOT_FOUND,
                        "The requested file does not exist."};
            }
        }
    }

    try {
        FindRetrowNested<WidgetError>(ep);
    } catch (const WidgetError &e) {
        switch (e.GetCode()) {
        case WidgetErrorCode::UNSPECIFIED:
            break;

        case WidgetErrorCode::WRONG_TYPE:
        case WidgetErrorCode::UNSUPPORTED_ENCODING:
            return {HTTP_STATUS_BAD_GATEWAY, "Malformed widget response"};

        case WidgetErrorCode::NO_SUCH_VIEW:
            return {HTTP_STATUS_NOT_FOUND, "No such view"};

        case WidgetErrorCode::NOT_A_CONTAINER:
            return Dup(pool, HTTP_STATUS_NOT_FOUND, e.what());

        case WidgetErrorCode::FORBIDDEN:
            return {HTTP_STATUS_FORBIDDEN, "Forbidden"};
        }
    }

    try {
        FindRetrowNested<HttpClientError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Upstream server failed"};
    }

    try {
        FindRetrowNested<AjpClientError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Upstream server failed"};
    }

    try {
        FindRetrowNested<WasError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Script failed"};
    }

    try {
        FindRetrowNested<FcgiClientError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Script failed"};
    }

    try {
        FindRetrowNested<CgiError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Script failed"};
    }

    try {
        FindRetrowNested<MemcachedClientError>(ep);
    } catch (...) {
        return {HTTP_STATUS_BAD_GATEWAY, "Cache server failed"};
    }

    try {
        FindRetrowNested<NfsClientError>(ep);
    } catch (const NfsClientError &e) {
        switch (e.GetCode()) {
        case NFS3ERR_NOENT:
        case NFS3ERR_NOTDIR:
            return {HTTP_STATUS_NOT_FOUND,
                    "The requested file does not exist."};
        }
    }

    return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal server error"};
}

void
Request::LogDispatchError(http_status_t status,
                          const char *msg, const char *log_msg,
                          unsigned log_level)
{
    logger(log_level, "error on '", request.uri, "': ", log_msg);

    if (instance.config.verbose_response)
        msg = p_strdup(&pool, log_msg);

    DispatchResponse(status, msg);
}

void
Request::LogDispatchError(http_status_t status, const char *log_msg,
                          unsigned log_level)
{
    LogDispatchError(status, http_status_to_string(status),
                     log_msg, log_level);
}

void
Request::LogDispatchError(std::exception_ptr ep)
{
    logger(2, "error on '", request.uri, "': ", ep);

    auto response = ToResponse(pool, ep);
    if (instance.config.verbose_response)
        response.message = p_strdup(&pool, GetFullMessage(ep).c_str());

    DispatchResponse(response.status, response.message);
}

void
Request::LogDispatchError(http_status_t status, const char *msg,
                          std::exception_ptr ep, unsigned log_level)
{
    logger(log_level, "error on '", request.uri, "': ", ep);

    if (instance.config.verbose_response)
        msg = p_strdup(&pool, GetFullMessage(ep).c_str());

    DispatchResponse(status, msg);
}
