/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "session.h"

void
processor_env_init(pool_t pool, struct processor_env *env,
                   const char *partition_domain,
                   const char *remote_host,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   struct strmap *args,
                   session_id_t session_id,
                   struct strmap *request_headers,
                   istream_t request_body)
{
    assert(session_id != 0);
    assert(request_body == NULL || !istream_has_handler(request_body));

    env->pool = pool;
    env->partition_domain = partition_domain;
    env->remote_host = remote_host;
    env->absolute_uri = absolute_uri;
    env->external_uri = uri;

    if (args == NULL)
        env->args = strmap_new(pool, 16);
    else
        env->args = args;

    env->request_headers = request_headers;
    env->request_body = request_body;

    env->session_id = session_id;
}
