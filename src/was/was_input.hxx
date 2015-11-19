/*
 * Web Application Socket protocol, input data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_INPUT_HXX
#define BENG_PROXY_WAS_INPUT_HXX

#include "glibfwd.hxx"

#include <stdint.h>

struct pool;
class Istream;
class WasInput;

struct WasInputHandler {
    /**
     * Istream::Close() has been called.
     */
    void (*close)(void *ctx);

    /**
     * All data was received from the pipe to the input buffer; we
     * don't need the pipe anymore for this request.
     *
     * Optional method.
     */
    void (*release)(void *ctx);

    void (*eof)(void *ctx);

    /**
     * The input was aborted prematurely, but the socket may be
     * reused.
     */
    void (*premature)(void *ctx);

    void (*abort)(void *ctx);
};

WasInput *
was_input_new(struct pool *pool, int fd,
              const WasInputHandler *handler, void *handler_ctx);

/**
 * @param error the error reported to the istream handler
 */
void
was_input_free(WasInput *input, GError *error);

static inline void
was_input_free_p(WasInput **input_p, GError *error)
{
    WasInput *input = *input_p;
    *input_p = nullptr;
    was_input_free(input, error);
}

/**
 * Like was_input_free(), but assumes that was_input_enable() has not
 * been called yet (no istream handler).
 */
void
was_input_free_unused(WasInput *input);

static inline void
was_input_free_unused_p(WasInput **input_p)
{
    WasInput *input = *input_p;
    *input_p = nullptr;
    was_input_free_unused(input);
}

Istream &
was_input_enable(WasInput &input);

/**
 * Set the new content length of this entity.
 *
 * @return false if the value is invalid (callback "abort" has been
 * invoked in this case)
 */
bool
was_input_set_length(WasInput *input, uint64_t length);

/**
 * Signals premature end of this stream.
 *
 * @param length the total number of bytes the peer has written to the
 * pipe
 * @return false if the object has been closed
 */
bool
was_input_premature(WasInput *input, uint64_t length);

void
was_input_enable_timeout(WasInput *input);

#endif
