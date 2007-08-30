/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"
#include "processor.h"
#include "embed.h"

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
        pool_unref(s->istream->pool);
        s->istream = NULL;
        /* XXX */
    }
}

static const struct istream_handler substitution_istream_handler = {
    .data = substitution_istream_data,
    .eof = substitution_istream_eof,
    .free = substitution_istream_free,
};

void
substitution_start(struct substitution *s, const char *url)
{
    assert(s != NULL);
    assert(s->handler != NULL);
    assert(url != NULL);

    s->istream = embed_new(s->pool, url);
    s->istream_eof = 0;

    pool_ref(s->istream->pool);

    s->istream->handler = &substitution_istream_handler;
    s->istream->handler_ctx = s;
    istream_read(s->istream);
}

void
substitution_close(struct substitution *s)
{
    assert(s != NULL);

    if (s->istream != NULL) {
        istream_close(s->istream);
        assert(s->istream == NULL);
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
