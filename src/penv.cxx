/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "penv.hxx"
#include "session.hxx"
#include "istream.h"
#include "strmap.h"
#include "istream.h"

processor_env::processor_env(struct pool *_pool,
                             const char *_site_name,
                             const char *_untrusted_host,
                             const char *_local_host,
                             const char *_remote_host,
                             const char *_request_uri,
                             const char *_absolute_uri,
                             const struct parsed_uri *_uri,
                             struct strmap *_args,
                             const char *_session_cookie,
                             session_id_t _session_id,
                             http_method_t _method,
                             struct strmap *_request_headers)
    :pool(_pool),
    site_name(_site_name), untrusted_host(_untrusted_host),
    local_host(_local_host), remote_host(_remote_host),
    uri(_request_uri), absolute_uri(_absolute_uri), external_uri(_uri),
    args(_args != nullptr ? _args : strmap_new(pool, 16)),
    path_info(strmap_remove(args, "path")),
    view_name(strmap_remove(args, "view")),
    method(_method),
    request_headers(_request_headers),
    session_cookie(_session_cookie),
    session_id(_session_id) {}
