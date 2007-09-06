/*
 * Embed a processed HTML document
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "url-stream.h"
#include "processor.h"

#include <assert.h>
#include <string.h>

struct embed {
    const struct widget *widget;
    const struct processor_env *env;

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
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        input = processor_new(embed->delayed->pool, body,
                              embed->widget, embed->env);
        if (input == NULL) {
            istream_close(body);
            input = istream_string_new(embed->delayed->pool, "Failed to create processor object.");
        }
    } else {
        istream_close(body);

        input = istream_string_new(embed->delayed->pool, "Not an HTML document");
    }

    istream_delayed_set(embed->delayed, input);
}

istream_t
embed_new(pool_t pool, const char *url, const struct widget *widget,
          const struct processor_env *env)
{
    struct embed *embed;

    assert(url != NULL);

    embed = p_malloc(pool, sizeof(*embed));
    embed->widget = widget;
    embed->env = env;
    embed->delayed = istream_delayed_new(pool, embed_abort, embed);

    embed->url_stream = url_stream_new(pool,
                                       HTTP_METHOD_GET, url, NULL,
                                       0, NULL, /* XXX request body */
                                       embed_http_client_callback, embed);
    if (embed->url_stream == NULL)
        istream_delayed_set(embed->delayed,
                            istream_string_new(pool, "Failed to create url_stream object."));

    return embed->delayed;
}
