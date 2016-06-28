#include "ResourceLoader.hxx"

class FailingResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     unsigned session_sticky,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     const struct http_response_handler &handler,
                     void *handler_ctx,
                     struct async_operation_ref &async_ref) override;
};
