#ifndef SINK_IMPL_H
#define SINK_IMPL_H

struct istream;
struct async_operation_ref;

void
sink_null_new(struct istream *istream);

void
sink_close_new(struct istream *istream);

#endif
