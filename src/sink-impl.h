#ifndef SINK_IMPL_H
#define SINK_IMPL_H

#include "istream.h"

struct async_operation_ref;

void
sink_null_new(istream_t istream);

void
sink_close_new(istream_t istream);

#endif
