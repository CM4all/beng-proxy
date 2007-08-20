/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SUBSTITUTION_H
#define __BENG_SUBSTITUTION_H

#include <sys/types.h>

struct substitution;

struct substitution_handler {
    void (*meta)(struct substitution *s,
                 const char *content_type, off_t length);
    void (*output)(struct substitution *s);
};

typedef size_t (*substitution_output_t)(const void *data, size_t length, void *ctx);

struct substitution {
    struct substitution *next;
    off_t start, end, content_length;

    const struct substitution_handler *handler;
    void *handler_ctx;
};

void
substitution_start(struct substitution *s);

size_t
substitution_output(struct substitution *s,
                    substitution_output_t callback, void *callback_ctx);

int
substitution_finished(const struct substitution *s);

#endif
