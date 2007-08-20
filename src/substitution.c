/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"

size_t
substitution_output(struct substitution *s,
                    substitution_callback_t callback, void *callback_ctx)
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
