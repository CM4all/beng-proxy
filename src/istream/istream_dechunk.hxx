/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DECHUNK_HXX
#define BENG_PROXY_ISTREAM_DECHUNK_HXX

struct pool;
class Istream;

/**
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
Istream *
istream_dechunk_new(struct pool *pool, Istream &input,
                    void (*eof_callback)(void *ctx), void *callback_ctx);

/**
 * Check if the parameter is an istream_dechunk, and if so, switch to
 * "verbatim" mode and return true.  May only be called on a pristine
 * object.
 *
 * In "verbatim" mode, this istream's output is still chunked, but
 * verified, and its end-of-file is detected.  This is useful when we
 * need to output chunked data (e.g. proxying to another client).
 */
bool
istream_dechunk_check_verbatim(Istream &i);

#endif
