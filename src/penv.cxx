/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "penv.hxx"
#include "strmap.hxx"

processor_env::processor_env(struct pool *_pool,
                             EventLoop &_event_loop,
                             ResourceLoader &_resource_loader,
                             ResourceLoader &_filter_resource_loader,
                             const char *_site_name,
                             const char *_untrusted_host,
                             const char *_local_host,
                             const char *_remote_host,
                             const char *_request_uri,
                             const char *_absolute_uri,
                             const struct parsed_uri *_uri,
                             struct strmap *_args,
                             const char *_session_cookie,
                             SessionId _session_id,
                             const char *_realm,
                             http_method_t _method,
                             struct strmap *_request_headers)
    :pool(_pool),
     event_loop(&_event_loop),
     resource_loader(&_resource_loader),
     filter_resource_loader(&_filter_resource_loader),
    site_name(_site_name), untrusted_host(_untrusted_host),
    local_host(_local_host), remote_host(_remote_host),
    uri(_request_uri), absolute_uri(_absolute_uri), external_uri(_uri),
    args(_args != nullptr ? _args : strmap_new(pool)),
    path_info(args->Remove("path")),
    view_name(args->Remove("view")),
    method(_method),
    request_headers(_request_headers),
    session_cookie(_session_cookie),
     session_id(_session_id), realm(_realm) {}
