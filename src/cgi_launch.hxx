/*
 * Launch a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_LAUNCH_HXX
#define BENG_PROXY_CGI_LAUNCH_HXX

#include "glibfwd.hxx"

#include <http/method.h>

struct pool;
struct istream;
struct cgi_address;
struct strmap;

struct istream *
cgi_launch(struct pool *pool, http_method_t method,
           const struct cgi_address *address,
           const char *remote_addr,
           struct strmap *headers, struct istream *body,
           GError **error_r);

#endif
