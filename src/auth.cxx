/*
 * #TRANSLATE_AUTH implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "tcache.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"
#include "pbuffer.hxx"
#include "TranslateHandler.hxx"
#include "translate_quark.hxx"
#include "load_file.hxx"
#include "http_quark.h"
#include "util/Exception.hxx"

#include <daemon/log.h>

static void
auth_translate_response(TranslateResponse &response, void *ctx)
{
    auto &request = *(Request *)ctx;

    bool is_authenticated = false;
    {
        auto session = request.ApplyTranslateSession(response);
        if (session)
            is_authenticated = session->user != nullptr;
    }

    if (request.CheckHandleRedirectBounceStatus(response))
        return;

    if (!is_authenticated) {
        /* for some reason, the translation server did not send
           REDIRECT/BOUNCE/STATUS, but we still don't have a user -
           this should not happen; bail out, don't dare to accept the
           client */
        response_dispatch_message(request, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    request.translate.user_modified = response.user != nullptr;

    request.OnTranslateResponseAfterAuth(*request.translate.previous);
}

static void
auth_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &request = *(Request *)ctx;

    response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                          "Translation server failed",
                          GetFullMessage(ep).c_str());
}

static constexpr TranslateHandler auth_translate_handler = {
    .response = auth_translate_response,
    .error = auth_translate_error,
};

void
Request::HandleAuth(const TranslateResponse &response)
{
    assert(response.HasAuth());

    auto auth = response.auth;
    if (auth.IsNull()) {
        /* load #TRANSLATE_AUTH_FILE */
        assert(response.auth_file != nullptr);

        GError *error = nullptr;
        auth = LoadFile(pool, response.auth_file, 64, &error);
        if (auth.IsNull()) {
            response_dispatch_error(*this, error);
            g_error_free(error);
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
    t->Clear();
    t->auth = auth;
    t->uri = request.uri;
    t->host = translate.request.host;
    t->session = translate.request.session;

    if (response.protocol_version >= 2)
        t->listener_tag = connection.listener_tag;

    translate.previous = &response;

    translate_cache(pool,
                    *instance.translate_cache,
                    *t,
                    auth_translate_handler, this,
                    cancel_ptr);
}

