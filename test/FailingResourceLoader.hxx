#include "ResourceLoader.hxx"

class FailingResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     sticky_hash_t session_sticky,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};
