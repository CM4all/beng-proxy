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

#include "DirectResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "filtered_socket.hxx"
#include "http_request.hxx"
#include "http_response.hxx"
#include "file_request.hxx"
#include "file_address.hxx"
#include "lhttp_request.hxx"
#include "http_address.hxx"
#include "http_headers.hxx"
#include "cgi/cgi_glue.hxx"
#include "cgi_address.hxx"
#include "fcgi/Request.hxx"
#include "fcgi/Remote.hxx"
#include "was/Glue.hxx"
#include "ajp/Glue.hxx"
#include "nfs/Address.hxx"
#include "nfs/Glue.hxx"
#include "header_writer.hxx"
#include "pipe_filter.hxx"
#include "delegate/Address.hxx"
#include "delegate/HttpRequest.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "ssl/Client.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"
#include "system/Error.hxx"
#include "net/HostParser.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringUtil.hxx"

#include <string.h>
#include <stdlib.h>

class SslSocketFilterFactory final : public SocketFilterFactory {
    EventLoop &event_loop;
    const char *const host;

public:
    SslSocketFilterFactory(EventLoop &_event_loop,
                           const char *_host)
        :event_loop(_event_loop), host(_host) {}

    void *CreateFilter() override {
        return ssl_client_create(event_loop, host);
    }
};

static const char *
extract_remote_addr(const StringMap *headers)
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

    return StripLeft(p);
}

static const char *
extract_remote_ip(struct pool *pool, const StringMap *headers)
{
    const char *p = extract_remote_addr(headers);
    if (p == nullptr)
        return p;

    auto eh = ExtractHost(p);
    if (eh.HasFailed() || eh.host.size == strlen(p))
        return p;

    return p_strdup(*pool, eh.host);
}

static const char *
extract_server_name(struct pool *pool, const StringMap *headers,
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
DirectResourceLoader::SendRequest(struct pool &pool,
                                  sticky_hash_t session_sticky,
                                  http_method_t method,
                                  const ResourceAddress &address,
                                  http_status_t status, StringMap &&headers,
                                  Istream *body,
                                  gcc_unused const char *body_etag,
                                  HttpResponseHandler &handler,
                                  CancellablePointer &cancel_ptr)
{
    switch (address.type) {
        const FileAddress *file;
        const CgiAddress *cgi;
        const NfsAddress *nfs;
        int stderr_fd;
        const char *server_name;
        unsigned server_port;
        const SocketFilter *filter;
        SocketFilterFactory *filter_factory;

    case ResourceAddress::Type::NONE:
        break;

    case ResourceAddress::Type::LOCAL:
        if (body != nullptr)
            /* static files cannot receive a request body, close it */
            body->CloseUnused();

        file = &address.GetFile();
        if (file->delegate != nullptr) {
            if (delegate_stock == nullptr) {
                handler.InvokeError(std::make_exception_ptr(std::runtime_error("No delegate stock")));
                return;
            }

            delegate_stock_request(event_loop, *delegate_stock, pool,
                                   file->delegate->delegate,
                                   file->delegate->child_options,
                                   file->path,
                                   file->content_type,
                                   handler,
                                   cancel_ptr);
            return;
        }

        static_file_get(event_loop, pool, file->path,
                        file->content_type,
                        handler);
        return;

    case ResourceAddress::Type::NFS:
        if (body != nullptr)
            /* NFS files cannot receive a request body, close it */
            body->CloseUnused();

        nfs = &address.GetNfs();

        nfs_request(pool, *nfs_cache,
                    nfs->server, nfs->export_name,
                    nfs->path, nfs->content_type,
                    handler, cancel_ptr);
        return;

    case ResourceAddress::Type::PIPE:
        cgi = &address.GetCgi();
        pipe_filter(spawn_service, event_loop, &pool,
                    cgi->path, cgi->args.ToArray(pool),
                    cgi->options,
                    status, std::move(headers), body,
                    handler);
        return;

    case ResourceAddress::Type::CGI:
        cgi_new(spawn_service, event_loop, &pool,
                method, &address.GetCgi(),
                extract_remote_ip(&pool, &headers),
                headers, body,
                handler, cancel_ptr);
        return;

    case ResourceAddress::Type::FASTCGI:
        cgi = &address.GetCgi();

        if (cgi->options.stderr_path != nullptr) {
            stderr_fd = cgi->options.OpenStderrPath();
            if (stderr_fd < 0) {
                int code = errno;

                if (body != nullptr)
                    body->CloseUnused();

                handler.InvokeError(std::make_exception_ptr(FormatErrno(code, "Failed to open '%s'",
                                                                        cgi->options.stderr_path)));
                return;
            }
        } else
            stderr_fd = -1;

        if (cgi->address_list.IsEmpty())
            fcgi_request(&pool, event_loop, fcgi_stock,
                         cgi->options,
                         cgi->action,
                         cgi->path,
                         cgi->args.ToArray(pool),
                         method, cgi->GetURI(&pool),
                         cgi->script_name,
                         cgi->path_info,
                         cgi->query_string,
                         cgi->document_root,
                         extract_remote_ip(&pool, &headers),
                         headers, body,
                         cgi->params.ToArray(pool),
                         stderr_fd,
                         handler, cancel_ptr);
        else
            fcgi_remote_request(&pool, event_loop, tcp_balancer,
                                &cgi->address_list,
                                cgi->path,
                                method, cgi->GetURI(&pool),
                                cgi->script_name,
                                cgi->path_info,
                                cgi->query_string,
                                cgi->document_root,
                                extract_remote_ip(&pool, &headers),
                                std::move(headers), body,
                                cgi->params.ToArray(pool),
                                stderr_fd,
                                handler, cancel_ptr);
        return;

    case ResourceAddress::Type::WAS:
        cgi = &address.GetCgi();
        was_request(pool, *was_stock, cgi->options,
                    cgi->action,
                    cgi->path,
                    cgi->args.ToArray(pool),
                    method, cgi->GetURI(&pool),
                    cgi->script_name,
                    cgi->path_info,
                    cgi->query_string,
                    headers, body,
                    cgi->params.ToArray(pool),
                    handler, cancel_ptr);
        return;

    case ResourceAddress::Type::HTTP:
        switch (address.GetHttp().protocol) {
        case HttpAddress::Protocol::HTTP:
            if (address.GetHttp().ssl) {
                filter = &ssl_client_get_filter();
                filter_factory = NewFromPool<SslSocketFilterFactory>(pool,
                                                                     event_loop,
                                                                     /* TODO: only host */
                                                                     address.GetHttp().host_and_port);
            } else {
                filter = nullptr;
                filter_factory = nullptr;
            }

            http_request(pool, event_loop, *tcp_balancer, session_sticky,
                         filter, filter_factory,
                         method, address.GetHttp(),
                         HttpHeaders(std::move(headers)), body,
                         handler, cancel_ptr);
            break;

        case HttpAddress::Protocol::AJP:
            server_port = 80;
            server_name = extract_server_name(&pool, &headers, &server_port);
            ajp_stock_request(pool, event_loop, *tcp_balancer,
                              session_sticky,
                              "http", extract_remote_ip(&pool, &headers),
                              nullptr,
                              server_name, server_port,
                              false,
                              method, address.GetHttp(),
                              std::move(headers), body,
                              handler, cancel_ptr);
            break;
        }

        return;

    case ResourceAddress::Type::LHTTP:
        lhttp_request(pool, event_loop, *lhttp_stock,
                      address.GetLhttp(),
                      method, HttpHeaders(std::move(headers)), body,
                      handler, cancel_ptr);
        return;
    }

    /* the resource could not be located, abort the request */

    if (body != nullptr)
        body->CloseUnused();

    handler.InvokeError(std::make_exception_ptr(std::runtime_error("Could not locate resource")));
}
