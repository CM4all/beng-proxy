#ifndef SINK_GSTRING_H
#define SINK_GSTRING_H

#include <glib.h>

struct pool;
struct istream;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

void
sink_gstring_new(struct pool *pool, struct istream *input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
