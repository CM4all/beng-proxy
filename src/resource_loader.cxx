/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource_loader.hxx"
#include "resource_address.hxx"
#include "http_request.hxx"
#include "http_response.h"
#include "static-file.h"
#include "file_address.hxx"
#include "lhttp_request.hxx"
#include "http_address.hxx"
#include "cgi_glue.hxx"
#include "cgi_address.hxx"
#include "fcgi_request.hxx"
#include "fcgi_remote.hxx"
#include "nfs_address.hxx"
#include "was_glue.hxx"
#include "ajp_request.hxx"
#include "header-writer.h"
#include "pipe_filter.hxx"
#include "delegate_request.hxx"
#include "strutil.h"
#include "strmap.h"
#include "istream.h"
#include "ssl_client.h"
#include "gerrno.h"

#ifdef HAVE_LIBNFS
#include "nfs_request.h"
#endif

#include <socket/parser.h>

#include <string.h>
#include <stdlib.h>

struct resource_loader {
    struct pool *pool;

    struct tcp_balancer *tcp_balancer;
    struct lhttp_stock *lhttp_stock;
    struct fcgi_stock *fcgi_stock;
    struct hstock *was_stock;
    struct hstock *delegate_stock;

#ifdef HAVE_LIBNFS
    struct nfs_cache *nfs_cache;
#endif
};

static inline GQuark
resource_loader_quark(void)
{
    return g_quark_from_static_string("resource_loader");
}

struct resource_loader *
resource_loader_new(struct pool *pool, struct tcp_balancer *tcp_balancer,
                    struct lhttp_stock *lhttp_stock,
                    struct fcgi_stock *fcgi_stock, struct hstock *was_stock,
                    struct hstock *delegate_stock,
                    struct nfs_cache *nfs_cache)
{
    assert(fcgi_stock != nullptr);

    auto rl = NewFromPool<struct resource_loader>(pool);

    rl->pool = pool;
    rl->tcp_balancer = tcp_balancer;
    rl->lhttp_stock = lhttp_stock;
    rl->fcgi_stock = fcgi_stock;
    rl->was_stock = was_stock;
    rl->delegate_stock = delegate_stock;

#ifdef HAVE_LIBNFS
    rl->nfs_cache = nfs_cache;
#else
    (void)nfs_cache;
#endif

    return rl;
}

static const char *
extract_remote_addr(const struct strmap *headers)
{
    const char *xff = strmap_get_checked(headers, "x-forwarded-for");
    if (xff == nullptr)
        return nullptr;

    /* extract the last host name in X-Forwarded-For */
    const char *p = strrchr(xff, ',');
    if (p == nullptr)
        p = xff;
    else
        ++p;

    while (*p == ' ')
        ++p;

    return p;
}

static const char *
extract_remote_ip(struct pool *pool, const struct strmap *headers)
{
    const char *p = extract_remote_addr(headers);
    if (p == nullptr)
        return p;

    size_t length;
    const char *endptr;
    const char *q = socket_extract_hostname(p, &length, &endptr);
    if (q == p && length == strlen(p))
        return p;

    return p_strndup(pool, q, length);
}

static const char *
extract_server_name(struct pool *pool, const struct strmap *headers,
                    unsigned *port_r)
{
    const char *p = strmap_get_checked(headers, "host");
    if (p == nullptr)
        return nullptr;

    const char *colon = strchr(p, ':');
    if (colon == nullptr)
        return p;

    if (strchr(colon + 1, ':') != nullptr)
        /* XXX handle IPv6 addresses properly */
        return p;

    char *endptr;
    unsigned port = strtoul(colon + 1, &endptr, 10);
    if (endptr > colon + 1 && *endptr == 0)
        *port_r = port;

    return p_strndup(pool, p, colon - p);
}

void
resource_loader_request(struct resource_loader *rl, struct pool *pool,
                        unsigned session_sticky,
                        http_method_t method,
                        const struct resource_address *address,
                        http_status_t status, struct strmap *headers,
                        struct istream *body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    assert(rl != nullptr);
    assert(pool != nullptr);
    assert(address != nullptr);

    switch (address->type) {
        const struct file_address *file;
        const struct cgi_address *cgi;
        int stderr_fd;
        const char *server_name;
        unsigned server_port;
        const struct socket_filter *filter;
        void *filter_ctx;

    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        if (body != nullptr)
            /* static files cannot receive a request body, close it */
            istream_close_unused(body);

        file = address->u.file;
        if (file->delegate != nullptr) {
            if (rl->delegate_stock == nullptr) {
                GError *error = g_error_new_literal(resource_loader_quark(), 0,
                                                    "No delegate stock");
                http_response_handler_direct_abort(handler, handler_ctx, error);
                return;
            }

            delegate_stock_request(rl->delegate_stock, pool,
                                   file->delegate,
                                   &file->child_options,
                                   file->path,
                                   file->content_type,
                                   handler, handler_ctx,
                                   async_ref);
            return;
        }

        static_file_get(pool, file->path,
                        file->content_type,
                        handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_NFS:
#ifdef HAVE_LIBNFS
        if (body != nullptr)
            /* NFS files cannot receive a request body, close it */
            istream_close_unused(body);

        nfs_request(pool, rl->nfs_cache,
                    address->u.nfs->server, address->u.nfs->export_name,
                    address->u.nfs->path,
                    address->u.nfs->content_type,
                    handler, handler_ctx, async_ref);
#else
        http_response_handler_direct_abort(handler, handler_ctx,
                                           g_error_new_literal(resource_loader_quark(), 0,
                                                               "libnfs disabled"));
#endif
        return;

    case RESOURCE_ADDRESS_PIPE:
        cgi = address->u.cgi;
        pipe_filter(pool, cgi->path,
                    cgi->args.values, cgi->args.n,
                    cgi->options,
                    status, headers, body,
                    handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_CGI:
        cgi_new(pool, method, address->u.cgi,
                extract_remote_ip(pool, headers),
                headers, body,
                handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_FASTCGI:
        cgi = address->u.cgi;

        if (cgi->options.stderr_path != nullptr) {
            stderr_fd = cgi->options.OpenStderrPath();
            if (stderr_fd < 0) {
                int code = errno;
                GError *error =
                    g_error_new(errno_quark(), code, "open('%s') failed: %s",
                                cgi->options.stderr_path,
                                g_strerror(code));
                http_response_handler_direct_abort(handler, handler_ctx,
                                                   error);
                return;
            }
        } else
            stderr_fd = -1;

        if (cgi->address_list.IsEmpty())
            fcgi_request(pool, rl->fcgi_stock,
                         &cgi->options,
                         cgi->action,
                         cgi->path,
                         cgi->args.values, cgi->args.n,
                         method, cgi->GetURI(pool),
                         cgi->script_name,
                         cgi->path_info,
                         cgi->query_string,
                         cgi->document_root,
                         extract_remote_ip(pool, headers),
                         headers, body,
                         cgi->params.values, cgi->params.n,
                         stderr_fd,
                         handler, handler_ctx, async_ref);
        else
            fcgi_remote_request(pool, rl->tcp_balancer,
                                &cgi->address_list,
                                cgi->path,
                                method, cgi->GetURI(pool),
                                cgi->script_name,
                                cgi->path_info,
                                cgi->query_string,
                                cgi->document_root,
                                extract_remote_ip(pool, headers),
                                headers, body,
                                cgi->params.values, cgi->params.n,
                                stderr_fd,
                                handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_WAS:
        cgi = address->u.cgi;
        was_request(pool, rl->was_stock, &cgi->options,
                    cgi->action,
                    cgi->path,
                    cgi->args.values, cgi->args.n,
                    method, cgi->GetURI(pool),
                    cgi->script_name,
                    cgi->path_info,
                    cgi->query_string,
                    headers, body,
                    cgi->params.values, cgi->params.n,
                    handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_HTTP:
        if (address->u.http->ssl) {
            GError *error = nullptr;
            filter_ctx = ssl_client_create(rl->pool, pool,
                                           /* TODO: only host */
                                           address->u.http->host_and_port,
                                           &error);
            if (filter_ctx == nullptr) {
                http_response_handler_direct_abort(handler, handler_ctx,
                                                   error);
                return;
            }

            filter = ssl_client_get_filter();
        } else {
            filter = nullptr;
            filter_ctx = nullptr;
        }

        http_request(pool, rl->tcp_balancer, session_sticky,
                     filter, filter_ctx,
                     method, address->u.http,
                     headers_dup(pool, headers), body,
                     handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_AJP:
        server_port = 80;
        server_name = extract_server_name(pool, headers, &server_port);
        ajp_stock_request(pool, rl->tcp_balancer, session_sticky,
                          "http", extract_remote_ip(pool, headers),
                          nullptr,
                          server_name, server_port,
                          false,
                          method, address->u.http,
                          headers, body,
                          handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_LHTTP:
        lhttp_request(pool, rl->lhttp_stock, address->u.lhttp,
                      method, headers_dup(pool, headers), body,
                      handler, handler_ctx, async_ref);
        return;
    }

    /* the resource could not be located, abort the request */

    if (body != nullptr)
        istream_close_unused(body);

    GError *error = g_error_new_literal(resource_loader_quark(), 0,
                                        "Could not locate resource");
    http_response_handler_direct_abort(handler, handler_ctx, error);
}
