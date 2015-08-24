/*
 * High level FastCGI client for remote FastCGI servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_REMOTE_HXX
#define BENG_PROXY_FCGI_REMOTE_HXX

#include <http/method.h>

struct pool;
struct istream;
struct TcpBalancer;
struct AddressList;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
template<typename T> struct ConstBuffer;

void
fcgi_remote_request(struct pool *pool, TcpBalancer *tcp_balancer,
                    const AddressList *address_list,
                    const char *path,
                    http_method_t method, const char *uri,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    struct strmap *headers, struct istream *body,
                    ConstBuffer<const char *> params,
                    int stderr_fd,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
