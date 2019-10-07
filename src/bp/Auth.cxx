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
 * #TRANSLATE_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "pool/pbuffer.hxx"
#include "translation/Cache.hxx"
#include "translation/Handler.hxx"
#include "load_file.hxx"
#include "util/Exception.hxx"

inline void
Request::OnAuthTranslateResponse(const TranslateResponse &response) noexcept
{
    bool is_authenticated = false;
    {
        auto session = ApplyTranslateSession(response);
        if (session)
            is_authenticated = session->user != nullptr;
    }

    if (CheckHandleRedirectBounceStatus(response))
        return;

    if (!is_authenticated) {
        /* for some reason, the translation server did not send
           REDIRECT/BOUNCE/STATUS, but we still don't have a user -
           this should not happen; bail out, don't dare to accept the
           client */
        DispatchResponse(HTTP_STATUS_FORBIDDEN, "Forbidden");
        return;
    }

    translate.user_modified = response.user != nullptr;

    OnTranslateResponseAfterAuth(*translate.previous);
}

inline void
Request::OnAuthTranslateError(std::exception_ptr ep) noexcept
{
    LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                     "Configuration server failed", ep, 1);
}

class AuthTranslateHandler final : public TranslateHandler {
    Request &request;

public:
    explicit AuthTranslateHandler(Request &_request) noexcept
        :request(_request) {}

    /* virtual methods from TranslateHandler */
    void OnTranslateResponse(TranslateResponse &response) noexcept override {
        request.OnAuthTranslateResponse(response);
    }

    void OnTranslateError(std::exception_ptr error) noexcept override {
        request.OnAuthTranslateError(error);
    }
};

void
Request::HandleAuth(const TranslateResponse &response)
{
    assert(response.HasAuth());

    auto auth = response.auth;
    if (auth == nullptr) {
        /* load #TRANSLATE_AUTH_FILE */
        assert(response.auth_file != nullptr);

        try {
            auth = LoadFile(pool, response.auth_file, 64);
        } catch (...) {
            LogDispatchError(std::current_exception());
            return;
        }
    } else {
        assert(response.auth_file == nullptr);
    }

    const auto auth_base = auth;

    if (!response.append_auth.IsNull()) {
        assert(!auth.IsNull());

        auth = LazyCatBuffer(pool, auth, response.append_auth);
    }

    /* we need to validate the session realm early */
    ApplyTranslateRealm(response, auth_base);

    bool is_authenticated = false;
    {
        auto session = GetRealmSession();
        if (session)
            is_authenticated = session->user != nullptr &&
                !session->user_expires.IsExpired();
    }

    if (is_authenticated) {
        /* already authenticated; we can skip the AUTH request */
        OnTranslateResponseAfterAuth(response);
        return;
    }

    auto t = NewFromPool<TranslateRequest>(pool);
    t->auth = auth;
    t->uri = request.uri;
    t->host = translate.request.host;
    t->session = translate.request.session;

    if (response.protocol_version >= 2) {
        t->listener_tag = connection.listener_tag;

        if (connection.auth_alt_host)
            t->alt_host = request.headers.Get("x-cm4all-althost");
    }

    translate.previous = &response;

    auto *auth_translate_handler =
        NewFromPool<AuthTranslateHandler>(pool, *this);

    instance.translation_service->SendRequest(pool, *t,
                                              stopwatch,
                                              *auth_translate_handler,
                                              cancel_ptr);
}

