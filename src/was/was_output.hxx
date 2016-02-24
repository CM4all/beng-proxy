/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_OUTPUT_HXX
#define BENG_PROXY_WAS_OUTPUT_HXX

#include "glibfwd.hxx"

#include <stdint.h>

struct pool;
class Istream;
class WasOutput;

struct WasOutputHandler {
    /**
     * Announces the length of the resource.
     *
     * @param true on success, false if the #WasOutput object has been
     * deleted
     */
    bool (*length)(uint64_t length, void *ctx);

    /**
     * The stream ended prematurely, but the #WasOutput object is
     * still ok.
     *
     * @param the number of bytes aready sent
     * @param true on success, false if the #WasOutput object has been
     * deleted
     */
    bool (*premature)(uint64_t length, GError *error, void *ctx);

    void (*eof)(void *ctx);
    void (*abort)(GError *error, void *ctx);
};

WasOutput *
was_output_new(struct pool &pool, int fd, Istream &input,
               const WasOutputHandler &handler, void *handler_ctx);

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
