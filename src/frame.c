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

    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == frame_widget_callback);
    assert(widget != NULL);

    if (widget->id == NULL || env->frame == NULL ||
        strcmp(widget->id, env->frame) != 0) {
        /* XXX what if the focus is on a sub widget? */
        return NULL;
    }

    widget->proxy = 1; /* set flag if it wasn't previously set */

    if (env->external_uri->query != NULL)
        widget->real_uri = p_strncat(pool,
                                     widget->real_uri, strlen(widget->real_uri),
                                     "?", 1,
                                     env->external_uri->query,
                                     env->external_uri->query_length,
                                     NULL);

    if (env->request_body != NULL) {
        method = HTTP_METHOD_POST; /* XXX which method? */
        request_content_length = env->request_content_length;
        request_body = istream_hold_new(pool, env->request_body);
        /* XXX what if there is no stream handler? or two? */
    }

    return embed_new(pool,
                     method, widget->real_uri,
                     request_content_length, request_body,
                     widget,
                     env, 0);
}
