/*
 * Helper functions for struct processor_env.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "args.h"
#include "uri.h"
#include "session.h"
#include "widget.h"

void
processor_env_init(pool_t pool, struct processor_env *env,
                   const struct parsed_uri *uri,
                   strmap_t request_headers,
                   off_t request_content_length,
                   istream_t request_body,
                   processor_widget_callback_t widget_callback)
{
    const char *session_id;

    env->external_uri = uri;

    if (uri->args == NULL) {
        env->args = strmap_new(pool, 16);
        env->frame = NULL;
        env->focus = NULL;
        session_id = NULL;
    } else {
        env->args = args_parse(pool, uri->args, uri->args_length);
        env->frame = widget_ref_parse(pool, strmap_get(env->args, "frame"));
        env->focus = widget_ref_parse(pool, strmap_get(env->args, "focus"));
        session_id = strmap_get(env->args, "session");
    }

    env->proxy_callback = NULL;

    env->request_headers = request_headers;
    env->request_content_length = request_content_length;
    env->request_body = request_body;

    env->session = NULL;
    if (session_id != NULL) {
        session_id_t session_id2 = session_id_parse(session_id);
        if (session_id2 != 0)
            env->session = session_get(session_id2);
    }

    if (env->session == NULL) {
        env->session = session_new();
        session_id_format(env->session_id_buffer, env->session->id);
        strmap_put(env->args, "session", env->session_id_buffer, 1);
    }

    env->widget_callback = widget_callback;
}
