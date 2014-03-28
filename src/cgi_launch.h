/*
 * Launch a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_LAUNCH_H
#define BENG_PROXY_CGI_LAUNCH_H

#include <http/method.h>

#include <glib.h>

struct pool;
struct istream;
struct cgi_address;
struct strmap;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
cgi_launch(struct pool *pool, http_method_t method,
           const struct cgi_address *address,
           const char *remote_addr,
           struct strmap *headers, struct istream *body,
           GError **error_r);

#ifdef __cplusplus
}
#endif

#endif
