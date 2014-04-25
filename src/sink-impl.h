#ifndef SINK_IMPL_H
#define SINK_IMPL_H

struct istream;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

void
sink_null_new(struct istream *istream);

void
sink_close_new(struct istream *istream);

#ifdef __cplusplus
}
#endif

#endif
