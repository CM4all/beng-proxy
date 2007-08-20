/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include "pool.h"

#include <sys/types.h>

typedef struct processor *processor_t;

struct processor_handler {
    void (*input)(void *ctx);
    void (*meta)(const char *content_type, void *ctx);
    size_t (*output)(const void *data, size_t length, void *ctx);
    void (*output_finished)(void *ctx);
    void (*free)(void *ctx);
};

processor_t attr_malloc
processor_new(pool_t pool,
              const struct processor_handler *handler, void *ctx);

void
processor_free(processor_t *processor_r);

size_t
processor_input(processor_t processor, const void *buffer, size_t length);

void
processor_input_finished(processor_t processor);

void
processor_output(processor_t processor);

#endif
