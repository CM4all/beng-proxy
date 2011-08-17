/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "session.h"
#include "strmap.h"
#include "istream.h"

void
processor_env_init(struct pool *pool, struct processor_env *env,
                   const char *site_name,
                   const char *untrusted_host,
                   const char *local_host,
                   const char *remote_host,
                   const char *request_uri,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   struct strmap *args,
                   session_id_t session_id,
                   http_method_t method,
                   struct strmap *request_headers,
                   struct istream *request_body)
{
    assert(request_body == NULL || !istream_has_handler(request_body));

    env->pool = pool;
    env->site_name = site_name;
    env->untrusted_host = untrusted_host;
    env->local_host = local_host;
    env->remote_host = remote_host;
    env->uri = request_uri;
    env->absolute_uri = absolute_uri;
    env->external_uri = uri;

    if (args == NULL)
        env->args = strmap_new(pool, 16);
    else
        env->args = args;

    env->path_info = strmap_remove(env->args, "path");
    env->view_name = strmap_remove(env->args, "view");

    env->method = method;
    env->request_headers = request_headers;
    env->request_body = request_body;

    env->session_id = session_id;
}
