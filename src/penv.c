/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "session.h"

void
processor_env_init(pool_t pool, struct processor_env *env,
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
                   istream_t request_body)
{
    assert(request_body == NULL || !istream_has_handler(request_body));

    env->pool = pool;
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

    env->method = method;
    env->request_headers = request_headers;
    env->request_body = request_body;

    env->session_id = session_id;
}
