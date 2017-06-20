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
class EventLoop;
class Istream;
class SpawnService;
struct CgiAddress;
class StringMap;

/**
 * Throws std::runtime_error on error.
 */
Istream *
cgi_launch(EventLoop &event_loop, struct pool *pool, http_method_t method,
           const CgiAddress *address,
           const char *remote_addr,
           const StringMap &headers, Istream *body,
           SpawnService &spawn_service);

#endif
