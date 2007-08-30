/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"
#include "processor.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static size_t
substitution_istream_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;

    return s->handler->output(s, data, length);
}

static void
substitution_istream_eof(void *ctx)
{
    struct substitution *s = ctx;

    s->istream->handler = NULL;
    pool_unref(s->istream->pool);
    s->istream = NULL;
    s->istream_eof = 1;

    s->handler->eof(s);
}

static void
substitution_istream_free(void *ctx)
{
    struct substitution *s = ctx;

    if (!s->istream_eof && s->istream != NULL) {
        /* abort the transfer */
        s->istream = NULL;
        /* XXX */
    }
}

static const struct istream_handler substitution_istream_handler = {
    .data = substitution_istream_data,
    .eof = substitution_istream_eof,
    .free = substitution_istream_free,
};

static void 
substitution_http_client_callback(http_status_t status, strmap_t headers,
                                  off_t content_length, istream_t body,
                                  void *ctx)
{
    struct substitution *s = ctx;
    const char *value;

    (void)content_length;

    assert(s->url_stream != NULL);
    s->url_stream = NULL;

    if (status == 0) {
        /* XXX */
        substitution_close(s);
        return;
    }

    s->istream = body;

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        s->istream = processor_new(s->pool, s->istream);
        if (s->istream == NULL) {
            abort();
        }
    }

    assert(s->istream->handler == NULL);

    pool_ref(s->istream->pool);

    s->istream->handler = &substitution_istream_handler;
    s->istream->handler_ctx = s;

    istream_read(s->istream);
}

void
substitution_start(struct substitution *s, const char *url)
{
    assert(s != NULL);
    assert(s->handler != NULL);
    assert(url != NULL);

    s->istream = NULL;
    s->istream_eof = 0;

    s->url_stream = url_stream_new(s->pool,
                                   HTTP_METHOD_GET, url, NULL,
                                   substitution_http_client_callback, s);
    if (s->url_stream == NULL) {
        /* XXX */
        return;
    }
}

void
substitution_close(struct substitution *s)
{
    assert(s != NULL);

    if (s->istream != NULL) {
        istream_close(s->istream);
        assert(s->istream == NULL);
        assert(s->url_stream == NULL);
    } else if (s->url_stream != NULL) {
        url_stream_close(s->url_stream);
        assert(s->url_stream == NULL);
    }

    if (s->pool != NULL) {
        pool_t pool = s->pool;
        s->pool = NULL;
        pool_unref(pool);
    }
}

void
substitution_output(struct substitution *s)
{
    if (s->istream != NULL)
        istream_read(s->istream);
}
