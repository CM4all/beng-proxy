/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_TIMEOUT_HXX
#define BENG_PROXY_ISTREAM_TIMEOUT_HXX

struct timeval;
struct pool;
class Istream;

/**
 * An istream that times out when no data has been received after a
 * certain amount of time.  The timer starts on the first
 * Istream::Read() call.
 */
Istream *
NewTimeoutIstream(struct pool &pool, Istream &input,
                  const struct timeval &timeout);

#endif
