/*
 * Hooks into external session managers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_external.hxx"
#include "session.hxx"
#include "bp_instance.hxx"
#include "background.hxx"
#include "http_address.hxx"
#include "http_request.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "istream/istream.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <glib.h>

class ExternalSessionRefresh final
    : public LinkedBackgroundJob, HttpResponseHandler {

    const HttpAddress address;

public:
    ExternalSessionRefresh(struct pool &pool,
                           BackgroundManager &_manager,
                           const HttpAddress &_address)
        :LinkedBackgroundJob(_manager),
         address(pool, _address) {}

    void SendRequest(struct pool &pool, BpInstance &instance,
                     const SessionId session_id) {
        http_request(pool, instance.event_loop, *instance.tcp_balancer,
                     session_id.GetClusterHash(),
                     nullptr, nullptr,
                     HTTP_METHOD_GET, address,
                     HttpHeaders(pool), nullptr,
                     *this, async_ref);
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status,
                        gcc_unused StringMap &&headers,
                        Istream *body) override {
        if (body != nullptr)
            body->CloseUnused();

        if (status < 200 || status >= 300)
            daemon_log(3, "Status %d from external session manager '%s'\n",
                       (int)status, address.path);

        Remove();
    }

    void OnHttpError(GError *error) override {
        daemon_log(2, "Failed to refresh external session: %s\n",
                   error->message);
        g_error_free(error);

        Remove();
    }
};

void
RefreshExternalSession(BpInstance &instance, Session &session)
{
    if (session.external_manager == nullptr ||
        session.external_keepalive <= std::chrono::seconds::zero())
        /* feature is not enabled */
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now < session.next_external_keepalive)
        /* not yet */
        return;

    daemon_log(5, "refresh external_session_manager '%s'\n",
               session.external_manager->path);

    session.next_external_keepalive = now + session.external_keepalive;

    struct pool *pool = pool_new_linear(instance.pool,
                                        "external_session_refresh", 4096);

    auto *refresh = NewFromPool<ExternalSessionRefresh>(*pool, *pool,
                                                        instance.background_manager,
                                                        *session.external_manager);
    instance.background_manager.Add(*refresh);

    refresh->SendRequest(*pool, instance, session.id);
    pool_unref(pool);
}
