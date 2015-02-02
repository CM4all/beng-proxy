/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_PIPE_HXX
#define BENG_PROXY_ISTREAM_PIPE_HXX

struct pool;
struct istream;
struct stock;

#ifdef __linux
struct istream *
istream_pipe_new(struct pool *pool, struct istream *input, struct stock *pipe_stock);
#endif

#endif
