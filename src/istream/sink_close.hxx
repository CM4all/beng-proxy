/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_CLOSE_HXX
#define BENG_PROXY_SINK_CLOSE_HXX

struct istream;

/**
 * An istream handler which closes the istream as soon as data
 * arrives.  This is used in the test cases.
 */
void
sink_close_new(struct istream *istream);

#endif
