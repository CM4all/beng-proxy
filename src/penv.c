/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "uri.h"
#include "session.h"
#include "widget.h"

void
processor_env_init(pool_t pool, struct processor_env *env,
                   struct hstock *http_client_stock,
                   const char *remote_host,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   strmap_t args,
                   struct session *session,
                   strmap_t request_headers,
                   off_t request_content_length,
                   istream_t request_body,
                   processor_widget_callback_t widget_callback)
{
    assert(request_body == NULL || !istream_has_handler(request_body));

    env->pool = pool;
    env->http_client_stock = http_client_stock;
    env->remote_host = remote_host;
    env->absolute_uri = absolute_uri;
    env->external_uri = uri;

    if (args == NULL)
        env->args = strmap_new(pool, 16);
    else
        env->args = args;

    http_response_handler_clear(&env->response_handler);

    env->request_headers = request_headers;
    env->request_content_length = request_content_length;
    env->request_body = request_body;

    env->session = session;

    if (env->session == NULL) {
        env->session = session_new();
        session_id_format(env->session_id_buffer, env->session->id);
        strmap_put(env->args, "session", env->session_id_buffer, 1);
    }

    env->widget_callback = widget_callback;
}
