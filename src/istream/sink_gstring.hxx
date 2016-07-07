/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SINK_GSTRING_HXX
#define SINK_GSTRING_HXX

#include "glibfwd.hxx"

struct pool;
class Istream;
class CancellablePointer;

void
sink_gstring_new(struct pool &pool, Istream &input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, CancellablePointer &cancel_ptr);

#endif
