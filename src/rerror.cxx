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

#include "request.hxx"
#include "bp_instance.hxx"
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

#include <daemon/log.h>

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
                break;
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
response_dispatch_log(Request &request, http_status_t status,
                      const char *msg, const char *log_msg)
{
    daemon_log(2, "error on '%s': %s\n", request.request.uri, log_msg);

    if (request.instance.config.verbose_response)
        msg = p_strdup(&request.pool, log_msg);

    response_dispatch_message(request, status, msg);
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *log_msg)
{
    response_dispatch_log(request, status,
                          http_status_to_string(status), log_msg);
}

void
response_dispatch_log(Request &request, std::exception_ptr ep)
{
    auto log_msg = GetFullMessage(ep);
    daemon_log(2, "error on '%s': %s\n", request.request.uri, log_msg.c_str());

    auto response = ToResponse(request.pool, ep);
    if (request.instance.config.verbose_response)
        response.message = p_strdup(&request.pool, log_msg.c_str());

    response_dispatch_message(request, response.status, response.message);
}
