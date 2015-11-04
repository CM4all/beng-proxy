/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_PIPE_HXX
#define BENG_PROXY_ISTREAM_PIPE_HXX

struct pool;
class Istream;
struct Stock;

#ifdef __linux
Istream *
istream_pipe_new(struct pool *pool, Istream &input, Stock *pipe_stock);
#endif

#endif
