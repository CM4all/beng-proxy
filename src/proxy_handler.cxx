/*
 * Serve HTTP requests from another HTTP/AJP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "request.hxx"
#include "request_forward.hxx"
#include "ResourceLoader.hxx"
#include "http_server/Request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "uri/uri_extract.hxx"
#include "http_response.hxx"
#include "istream/istream_pipe.hxx"
#include "lhttp_address.hxx"
#include "pool.hxx"

/**
 * Return a copy of the URI for forwarding to the next server.  This
 * omits the beng-proxy request "arguments".
 */
gcc_pure
static const char *
ForwardURI(struct pool &pool, const parsed_uri &uri)
{
    if (uri.query.IsEmpty())
        return p_strdup(pool, uri.base);
    else
        return p_strncat(&pool,
                         uri.base.data, uri.base.size,
                         "?", (size_t)1,
                         uri.query.data, uri.query.size,
                         nullptr);
}

/**
 * Return a copy of the original request URI for forwarding to the
 * next server.  This omits the beng-proxy request "arguments" (unless
 * the translation server declared the "transparent" mode).
 */
gcc_pure
static const char *
ForwardURI(const Request &r)
{
    const TranslateResponse &t = *r.translate.response;
    if (t.transparent || r.uri.args.IsNull())
        /* transparent or no args: return the full URI as-is */
        return r.request.uri;
    else
        /* remove the "args" part */
        return ForwardURI(r.pool, r.uri);
}

void
proxy_handler(Request &request2)
{
    struct pool &pool = request2.pool;
    const TranslateResponse &tr = *request2.translate.response;
    ResourceAddress address(ShallowCopy(), request2.translate.address);

    assert(address.type == ResourceAddress::Type::HTTP ||
           address.type == ResourceAddress::Type::LHTTP ||
           address.type == ResourceAddress::Type::NFS ||
           address.IsCgiAlike());

    if (request2.translate.response->transparent &&
        (!request2.uri.args.IsNull() ||
         !request2.uri.path_info.IsEmpty()))
        address = address.WithArgs(pool,
                                   request2.uri.args,
                                   request2.uri.path_info);

    if (!request2.processor_focus)
        /* forward query string */
        address = address.WithQueryStringFrom(pool, request2.request.uri);

    if (address.IsCgiAlike() &&
        address.GetCgi().script_name == nullptr &&
        address.GetCgi().uri == nullptr)
        /* pass the "real" request URI to the CGI (but without the
           "args", unless the request is "transparent") */
        address.GetCgi().uri = ForwardURI(request2);

    request2.cookie_uri = address.GetUriPath();

    auto forward = request_forward(request2,
                                   tr.request_header_forward,
                                   request2.GetCookieHost(),
                                   request2.GetCookieURI(),
                                   address.IsAnyHttp());

#ifdef SPLICE
    if (forward.body != nullptr)
        forward.body = istream_pipe_new(&pool, *forward.body,
                                        request2.instance.pipe_stock);
#endif

    for (const auto &i : tr.request_headers)
        forward.headers.SecureSet(i.key, i.value);

    request2.collect_cookies = true;

    request2.instance.cached_resource_loader
        ->SendRequest(pool,
                      request2.session_id.GetClusterHash(),
                      forward.method, address, HTTP_STATUS_OK,
                      std::move(forward.headers), forward.body, nullptr,
                      request2, request2.cancel_ptr);
}
