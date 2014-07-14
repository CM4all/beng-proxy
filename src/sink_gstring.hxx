/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SINK_GSTRING_HXX
#define SINK_GSTRING_HXX

#include "glibfwd.hxx"

struct pool;
struct istream;
struct async_operation_ref;

void
sink_gstring_new(struct pool *pool, struct istream *input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, struct async_operation_ref *async_ref);

#endif
