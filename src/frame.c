/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "embed.h"
#include "uri.h"
#include "processor.h"
#include "widget.h"

#include <assert.h>
#include <string.h>

istream_t
frame_widget_callback(pool_t pool, const struct processor_env *env,
                      struct widget *widget)
{
    http_method_t method = HTTP_METHOD_GET;
    off_t request_content_length = 0;
    istream_t request_body = NULL;
    struct processor_env *env2;

    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == frame_widget_callback);
    assert(widget != NULL);

    if (widget->id == NULL || env->frame == NULL ||
        !widget_ref_compare(pool, widget, env->frame, 1)) {
        /* XXX what if the focus is on a sub widget? */
        return NULL;
    }

    if (!widget_ref_compare(pool, widget, env->frame, 0))
        /* only partial match: this is the parent of the frame
           widget */
        return embed_new(pool,
                         method, widget->real_uri,
                         request_content_length, request_body,
                         widget,
                         env, PROCESSOR_QUIET);

    widget->from_request.proxy = 1; /* set flag if it wasn't previously set */

    if (!strref_is_empty(&env->external_uri->query))
        widget->real_uri = p_strncat(pool,
                                     widget->real_uri, strlen(widget->real_uri),
                                     "?", (size_t)1,
                                     env->external_uri->query.data,
                                     env->external_uri->query.length,
                                     NULL);

    if (env->request_body != NULL) {
        method = HTTP_METHOD_POST; /* XXX which method? */
        request_content_length = env->request_content_length;
        request_body = env->request_body;
        /* XXX what if there is no stream handler? or two? */
    }

    /* install normal embed callback on cloned env */

    env2 = processor_env_dup(pool, env);
    env2->frame = NULL;
    env2->widget_callback = embed_widget_callback;

    return embed_new(pool,
                     method, widget->real_uri,
                     request_content_length, request_body,
                     widget,
                     env2, 0);
}
