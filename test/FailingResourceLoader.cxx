#include "FailingResourceLoader.hxx"
#include "istream/istream.hxx"
#include "http_response.hxx"

#include <glib.h>

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

void
FailingResourceLoader::SendRequest(struct pool &,
                                   unsigned,
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

    handler.InvokeError(g_error_new(test_quark(), 0, "unimplemented"));
}
