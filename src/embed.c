/*
 * Embed a processed HTML document
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "url-stream.h"
#include "processor.h"
#include "widget.h"

#include <assert.h>
#include <string.h>

struct embed {
    struct widget *widget;
    const struct processor_env *env;
    unsigned options;

    url_stream_t url_stream;

    istream_t delayed;
};

static void
embed_abort(void *ctx)
{
    struct embed *embed = (struct embed *)ctx;

    if (embed->url_stream == NULL)
        return;

    embed->delayed = NULL;

    url_stream_close(embed->url_stream);
    assert(embed->url_stream == NULL);
}

static void 
embed_http_client_callback(http_status_t status, strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx)
{
    struct embed *embed = ctx;
    const char *value;
    istream_t input;

    (void)content_length;

    assert(embed->url_stream != NULL);
    embed->url_stream = NULL;

    if (status == 0) {
        /* XXX */
        if (embed->delayed != NULL)
            istream_free(&embed->delayed);
        return;
    }

    value = strmap_get(headers, "content-type");
    if (embed->widget->iframe) {
        /* XXX somehow propagate content-type header to http-server.c */
        input = body;
    } else if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        input = processor_new(istream_pool(embed->delayed), body,
                              embed->widget, embed->env, embed->options);
    } else {
        istream_close(body);

        if (embed->widget->id == NULL) {
            input = NULL;
        } else {
            /* it cannot be inserted into the HTML stream, so put it into
               an iframe */
            embed->widget->iframe = 1;
            input = embed->env->widget_callback(istream_pool(embed->delayed),
                                                embed->env, embed->widget);
        }

        if (input == NULL)
            input = istream_string_new(istream_pool(embed->delayed),
                                       "Not an HTML document");
    }

    istream_delayed_set(embed->delayed, input);
}

istream_t
embed_new(pool_t pool, http_method_t method, const char *url,
          off_t request_content_length,
          istream_t request_body,
          struct widget *widget,
          const struct processor_env *env,
          unsigned options)
{
    struct embed *embed;

    assert(url != NULL);

    embed = p_malloc(pool, sizeof(*embed));
    embed->widget = widget;
    embed->env = env;
    embed->options = options;
    embed->delayed = istream_delayed_new(pool, embed_abort, embed);

    embed->url_stream = url_stream_new(pool,
                                       method, url, NULL,
                                       request_content_length,
                                       request_body,
                                       embed_http_client_callback, embed);
    if (embed->url_stream == NULL)
        istream_delayed_set(embed->delayed,
                            istream_string_new(pool, "Failed to create url_stream object."));

    return embed->delayed;
}
