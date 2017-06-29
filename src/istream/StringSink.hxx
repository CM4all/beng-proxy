/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef STRING_SINK_HXX
#define STRING_SINK_HXX

#include <exception>
#include <string>

struct pool;
class Istream;
class CancellablePointer;

void
NewStringSink(struct pool &pool, Istream &input,
              void (*callback)(std::string &&value, std::exception_ptr error,
                               void *ctx),
              void *ctx, CancellablePointer &cancel_ptr);

#endif
