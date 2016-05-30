/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DECHUNK_HXX
#define BENG_PROXY_ISTREAM_DECHUNK_HXX

struct pool;
class EventLoop;
class Istream;

class DechunkHandler {
public:
    /**
     * Called as soon as the dechunker has seen the end chunk in data
     * provided by the input.  At this time, the end chunk may not yet
     * ready to be processed, but it's an indicator that input's
     * underlying socket is done.
     */
    virtual void OnDechunkEndSeen() = 0;

    /**
     * Called after the end chunk has been consumed from the input,
     * right before calling IstreamHandler::OnEof().
     *
     * @return false if the caller shall close its input
     */
    virtual bool OnDechunkEnd() = 0;
};

/**
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
Istream *
istream_dechunk_new(struct pool &pool, Istream &input,
                    EventLoop &event_loop,
                    DechunkHandler &dechunk_handler);

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
