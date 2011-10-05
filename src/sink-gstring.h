#ifndef SINK_GSTRING_H
#define SINK_GSTRING_H

#include "istream.h"

#include <glib.h>

struct pool;
struct async_operation_ref;

void
sink_gstring_new(struct pool *pool, istream_t input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, struct async_operation_ref *async_ref);

#endif
