/*
 * istream facade that ignores read() calls until it is resumed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_PAUSE_H
#define BENG_PROXY_ISTREAM_PAUSE_H

struct pool;
struct istream;

struct istream *
istream_pause_new(struct pool *pool, struct istream *input);

void
istream_pause_resume(struct istream *istream);

#endif
