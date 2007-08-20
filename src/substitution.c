/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"

#include <assert.h>
#include <stdlib.h>

void
substitution_start(struct substitution *s)
{
    assert(s != NULL);
    assert(s->handler != NULL);
    assert(s->handler->meta != NULL);

    s->handler->meta(s, "text/html", 0);
}

size_t
substitution_output(struct substitution *s,
                    substitution_output_t callback, void *callback_ctx)
{
    (void)s;
    (void)callback;
    (void)callback_ctx;
    return 0;
}

int
substitution_finished(const struct substitution *s)
{
    (void)s;
    return 1;
}
