/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SINK_GSTRING_HXX
#define SINK_GSTRING_HXX

#include "glibfwd.hxx"

#include <exception>

struct pool;
class Istream;
class CancellablePointer;

void
sink_gstring_new(struct pool &pool, Istream &input,
                 void (*callback)(GString *value, std::exception_ptr error,
                                  void *ctx),
                 void *ctx, CancellablePointer &cancel_ptr);

#endif
