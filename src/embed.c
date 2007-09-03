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
    struct istream output;

    const char *base_uri;

    url_stream_t url_stream;
    istream_t input;
    int input_eof, direct_mode;
};

static size_t
embed_input_data(const void *data, size_t length, void *ctx)
{
    struct embed *embed = ctx;

    return istream_invoke_data(&embed->output, data, length);
}

static ssize_t
embed_input_direct(int fd, size_t max_length, void *ctx)
{
    struct embed *embed = ctx;

    return istream_invoke_direct(&embed->output, fd, max_length);
}

static void
embed_input_eof(void *ctx)
{
    struct embed *embed = ctx;

    embed->input->handler = NULL;
    pool_unref(embed->input->pool);
    embed->input = NULL;
    embed->input_eof = 1;

    pool_ref(embed->output.pool);
    istream_invoke_eof(&embed->output);
    istream_close(&embed->output);
    pool_unref(embed->output.pool);
}

static void
embed_input_free(void *ctx)
{
    struct embed *embed = ctx;

    if (!embed->input_eof && embed->input != NULL) {
        /* abort the transfer */
        pool_unref(embed->input->pool);
        embed->input = NULL;
        /* XXX */
    }
}

static const struct istream_handler embed_input_handler = {
    .data = embed_input_data,
    .direct = embed_input_direct,
    .eof = embed_input_eof,
    .free = embed_input_free,
};

static inline struct embed *
istream_to_embed(istream_t istream)
{
    return (struct embed *)(((char*)istream) - offsetof(struct embed, output));
}

static void
istream_embed_read(istream_t istream)
{
    struct embed *embed = istream_to_embed(istream);

    if (embed->input != NULL)
        istream_read(embed->input);
}

static void
istream_embed_direct(istream_t istream)
{
    struct embed *embed = istream_to_embed(istream);

    if (embed->input != NULL)
        istream_direct(embed->input);
    else
        embed->direct_mode = 1;
}

static void
istream_embed_close(istream_t istream)
{
    struct embed *embed = istream_to_embed(istream);

    if (embed->url_stream != NULL) {
        assert(embed->input == NULL);
        url_stream_close(embed->url_stream);
        assert(embed->url_stream == NULL);
    } else if (embed->input != NULL) {
        assert(embed->url_stream == NULL);
        istream_close(embed->input);
        assert(embed->input == NULL);
    }

    istream_invoke_free(&embed->output);
}

static const struct istream istream_embed = {
    .read = istream_embed_read,
    .direct = istream_embed_direct,
    .close = istream_embed_close,
};

static void 
embed_http_client_callback(http_status_t status, strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx)
{
    struct embed *embed = ctx;
    const char *value;

    (void)content_length;

    assert(embed->url_stream != NULL);
    embed->url_stream = NULL;

    if (status == 0) {
        /* XXX */
        istream_close(&embed->output);
        return;
    }

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        embed->input = processor_new(embed->output.pool, body, embed->base_uri, NULL); /* XXX args */
        if (embed->input == NULL) {
            istream_close(body);
            embed->input = istream_string_new(embed->output.pool, "Failed to create processor object.");
        }
    } else {
        istream_close(body);

        embed->input = istream_string_new(embed->output.pool, "Not an HTML document");
    }

    pool_ref(embed->input->pool);
    embed->input->handler = &embed_input_handler;
    embed->input->handler_ctx = embed;

    if (embed->direct_mode)
        istream_direct(embed->input);
    else
        istream_read(embed->input);
}

istream_t
embed_new(pool_t pool, const char *url, const char *base_uri)
{
    struct embed *embed;

    assert(url != NULL);

    embed = p_malloc(pool, sizeof(*embed));
    embed->output = istream_embed;
    embed->output.pool = pool;
    embed->base_uri = base_uri;
    embed->input = NULL;
    embed->input_eof = 0;
    embed->direct_mode = 0;

    embed->url_stream = url_stream_new(pool,
                                       HTTP_METHOD_GET, url, NULL,
                                       embed_http_client_callback, embed);
    if (embed->url_stream == NULL) {
        embed->input = istream_string_new(pool, "Failed to create url_stream object.");

        pool_ref(embed->input->pool);
        embed->input->handler = &embed_input_handler;
        embed->input->handler_ctx = embed;
    }

    return &embed->output;
}
