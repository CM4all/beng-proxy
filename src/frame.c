/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "embed.h"
#include "processor.h"
#include "widget.h"

#include <daemon/log.h>

#include <assert.h>

static istream_t
frame_top_widget(pool_t pool, struct processor_env *env,
                 struct widget *widget)
{
    struct processor_env *env2;

    assert(widget->from_request.proxy);

    /* install normal embed callback on cloned env */

    env2 = processor_env_dup(pool, env);
    env2->widget_callback = embed_widget_callback;

    /* clear the requrst body in the original env - the function
       frame_widget_callback() has already discarded a request body
       that is not being used within the frame, and if
       env->request_body is still set, this means that the body is for
       the frame */
    env->request_body = NULL;

    /* clear the response_handler in the original env, because it is
       reserved for us, and the other widgets should not use it
       anymore */
    http_response_handler_clear(&env->response_handler);

    return embed_new(pool, widget,
                     env2,
                     PROCESSOR_JSCRIPT | PROCESSOR_JSCRIPT_ROOT);
}

istream_t
frame_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget)
{
    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == frame_widget_callback);
    assert(widget != NULL);

    if (widget->from_request.proxy)
        /* this widget is being proxied */
        return frame_top_widget(pool, env, widget);
    else if (widget->from_request.proxy_ref != NULL) {
        /* only partial match: this is the parent of the frame
           widget */

        if (env->request_body != NULL && widget->from_request.focus_ref == NULL) {
            /* the request body is not consumed yet, but the focus is not
               within the frame: discard the body, because it cannot ever
               be used */
            assert(!istream_has_handler(env->request_body));

            daemon_log(4, "discarding non-framed request body\n");

            istream_free(&env->request_body);
        }

        return embed_new(pool, widget,
                         env, PROCESSOR_QUIET);
    } else {
        /* this widget is none of our business */
        widget_cancel(widget);
        return NULL;
    }
}
