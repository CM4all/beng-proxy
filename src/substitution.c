/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"

#include <assert.h>
#include <stdlib.h>

static const char data[] = "<em>this is the substitution text</em>";

void
substitution_start(struct substitution *s)
{
    assert(s != NULL);
    assert(s->handler != NULL);
    assert(s->handler->meta != NULL);

    s->position = 0;

    s->handler->meta(s, "text/html", sizeof(data) - 1);
}

void
substitution_close(struct substitution *s)
{
    assert(s != NULL);

    (void)s;
}

size_t
substitution_output(struct substitution *s,
                    substitution_output_t callback, void *callback_ctx)
{
    size_t nbytes;

    nbytes = callback(data + s->position, sizeof(data) - 1 - s->position, callback_ctx);
    s->position += nbytes;

    return nbytes;
}

int
substitution_finished(const struct substitution *s)
{
    return s->position == sizeof(data) - 1;
}
