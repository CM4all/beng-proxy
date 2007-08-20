/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SUBSTITUTION_H
#define __BENG_SUBSTITUTION_H

#include <sys/types.h>

struct substitution {
    struct substitution *next;
    off_t start, end;
};

typedef size_t (*substitution_callback_t)(const void *data, size_t length, void *ctx);

size_t
substitution_output(struct substitution *s,
                    substitution_callback_t callback, void *callback_ctx);

int
substitution_finished(const struct substitution *s);

#endif
