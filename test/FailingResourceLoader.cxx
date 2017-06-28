#include "FailingResourceLoader.hxx"
#include "istream/istream.hxx"
#include "http_response.hxx"

#include <stdexcept>

void
FailingResourceLoader::SendRequest(struct pool &,
                                   sticky_hash_t,
                                   http_method_t,
                                   const ResourceAddress &,
                                   http_status_t,
                                   StringMap &&,
                                   Istream *body, const char *,
                                   HttpResponseHandler &handler,
                                   CancellablePointer &)
{
    if (body != nullptr)
        body->CloseUnused();

    handler.InvokeError(std::make_exception_ptr(std::runtime_error("unimplemented")));
}
