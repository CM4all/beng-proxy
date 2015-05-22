/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_NULL_HXX
#define BENG_PROXY_SINK_NULL_HXX

struct istream;

/**
 * An istream handler which silently discards everything and ignores errors.
 */
void
sink_null_new(struct istream *istream);

#endif
