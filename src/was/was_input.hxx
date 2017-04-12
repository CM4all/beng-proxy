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
class EventLoop;
class Istream;
class WasInput;

class WasInputHandler {
public:
    /**
     * Istream::Close() has been called.
     *
     * The #Istream will be destroyed right after returning from this
     * method; the method should abandon all pointers to it, and not
     * call it.
     *
     * @param received the number of bytes received so far (includes
     * data that hasn't been delivered to the #IstreamHandler yet)
     */
    virtual void WasInputClose(uint64_t received) = 0;

    /**
     * All data was received from the pipe to the input buffer; we
     * don't need the pipe anymore for this request.
     *
     * @return false if the #WasInput has been destroyed by this
     * method
     */
    virtual bool WasInputRelease() = 0;

    /**
     * Called right before reporting end-of-file to the #IstreamHandler.
     *
     * The #Istream will be destroyed right after returning from this
     * method; the method should abandon all pointers to it, and not
     * call it.
     */
    virtual void WasInputEof() = 0;

    /**
     * There was an I/O error on the pipe.  Called right before
     * reporting the error to the #IstreamHandler.
     *
     * The #Istream will be destroyed right after returning from this
     * method; the method should abandon all pointers to it, and not
     * call it.
     */
    virtual void WasInputError() = 0;
};

WasInput *
was_input_new(struct pool &pool, EventLoop &event_loop, int fd,
              WasInputHandler &handler);

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
 * @return true if recovery was successful, false if the object has
 * been closed
 */
bool
was_input_premature(WasInput *input, uint64_t length);

/**
 * Same as above, but return the #GError instead of reporting it to
 * the #IstreamHandler.
 */
bool
was_input_premature(WasInput *input, uint64_t length, GError **error_r);

void
was_input_enable_timeout(WasInput *input);

#endif
