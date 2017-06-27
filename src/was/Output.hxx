/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_OUTPUT_HXX
#define BENG_PROXY_WAS_OUTPUT_HXX

#include <exception>

#include <stdint.h>

struct pool;
class EventLoop;
class FileDescriptor;
class Istream;
class WasOutput;

class WasOutputHandler {
public:
    /**
     * Announces the length of the resource.
     *
     * @param true on success, false if the #WasOutput object has been
     * deleted
     */
    virtual bool WasOutputLength(uint64_t length) = 0;

    /**
     * The stream ended prematurely, but the #WasOutput object is
     * still ok.
     *
     * @param the number of bytes aready sent
     * @param true on success, false if the #WasOutput object has been
     * deleted
     */
    virtual bool WasOutputPremature(uint64_t length,
                                    std::exception_ptr ep) = 0;

    virtual void WasOutputEof() = 0;

    virtual void WasOutputError(std::exception_ptr ep) = 0;
};

WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
               FileDescriptor fd, Istream &input,
               WasOutputHandler &handler);

/**
 * @return the total number of bytes written to the pipe
 */
uint64_t
was_output_free(WasOutput *data);

static inline uint64_t
was_output_free_p(WasOutput **output_p)
{
    WasOutput *output = *output_p;
    *output_p = nullptr;
    return was_output_free(output);
}

/**
 * Check if we can provide the LENGTH header.
 *
 * @return the WasOutputHandler::length() return value
 */
bool
was_output_check_length(WasOutput &output);

#endif
