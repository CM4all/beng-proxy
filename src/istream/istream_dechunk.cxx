/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_dechunk.hxx"
#include "http/ChunkParser.hxx"
#include "FacadeIstream.hxx"
#include "pool.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"

#include <algorithm>

#include <glib.h>

#include <assert.h>
#include <string.h>

class DechunkIstream final : public FacadeIstream {
    HttpChunkParser parser;

    bool eof = false, closed = false;

    bool had_input, had_output;

    /**
     * Copy chunked data verbatim to handler?
     *
     * @see istream_dechunk_check_verbatim()
     */
    bool verbatim = false;

    /**
     * Was the end-of-file chunk seen at the end of #pending_verbatim?
     */
    bool eof_verbatim;

    bool seen_eof = false;

    /**
     * Number of data chunk bytes already seen, but not yet consumed
     * by our #IstreamHandler.  In verbatim mode, this attribute is
     * unused.
     */
    size_t seen_data = 0;

    /**
     * Number of bytes to be passed to handler verbatim, which have
     * already been parsed but have not yet been consumed by the
     * handler.
     */
    size_t pending_verbatim;

    /**
     * This event is used to defer an DechunkHandler::OnDechunkEnd()
     * call.
     */
    DeferEvent defer_eof_event;

    DechunkHandler &dechunk_handler;

public:
    DechunkIstream(struct pool &p, Istream &_input,
                   DechunkHandler &_dechunk_handler)
        :FacadeIstream(p, _input),
         defer_eof_event(MakeSimpleEventCallback(DechunkIstream, DeferredEof),
                         this),
         dechunk_handler(_dechunk_handler)
    {
    }

    static DechunkIstream *CheckCast(Istream &i) {
        return dynamic_cast<DechunkIstream *>(&i);
    }

    void SetVerbatim() {
        verbatim = true;
        eof_verbatim = false;
        pending_verbatim = 0;
    }

private:
    void Abort(GError *error);

    gcc_pure
    bool IsEofPending() const {
        return defer_eof_event.IsPending();
    }

    void DeferredEof();

    /**
     * @return false if the input has been closed
     */
    bool EofDetected();

    bool CalculateRemainingDataSize(const char *src, const char *src_end);

    size_t Feed(const void *data, size_t length);

public:
    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    void _Read() override;
    void _Close() override;

protected:
    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

void
DechunkIstream::Abort(GError *error)
{
    assert(!closed);
    assert(!parser.HasEnded());
    assert(input.IsDefined());
    assert(!IsEofPending());

    closed = true;

    defer_eof_event.Cancel();
    input.ClearAndClose();
    DestroyError(error);
}

void
DechunkIstream::DeferredEof()
{
    assert(parser.HasEnded());
    assert(!input.IsDefined());
    assert(!eof);
    assert(!closed);

    eof = true;

    DestroyEof();
}

bool
DechunkIstream::EofDetected()
{
    assert(input.IsDefined());
    assert(parser.HasEnded());

    defer_eof_event.Add();

    bool result = dechunk_handler.OnDechunkEnd();
    if (result)
        input.Clear();
    else
        /* this code path is only used by the unit test */
        input.ClearAndClose();

    return result;
}

inline bool
DechunkIstream::CalculateRemainingDataSize(const char *src,
                                           const char *const src_end)
{
    assert(!IsEofPending());
    assert(!eof);

    seen_data = 0;

    if (parser.HasEnded()) {
        if (!seen_eof) {
            seen_eof = true;
            dechunk_handler.OnDechunkEndSeen();
        }

        return true;
    }

    /* work with a copy of our HttpChunkParser */
    HttpChunkParser p(parser);

    while (src != src_end) {
        GError *error = nullptr;
        const ConstBuffer<char> src_remaining(src, src_end - src);
        auto data = ConstBuffer<char>::FromVoid(p.Parse(src_remaining.ToVoid(),
                                                        &error));
        if (data.IsNull()) {
            Abort(error);
            return false;
        }

        if (data.IsEmpty()) {
            if (p.HasEnded() && !seen_eof) {
                seen_eof = true;
                dechunk_handler.OnDechunkEndSeen();
            }

            break;
        }

        seen_data += data.size;
        p.Consume(data.size);
        src = data.end();
    }

    return true;
}

size_t
DechunkIstream::Feed(const void *data0, size_t length)
{
    assert(!closed);
    assert(input.IsDefined());
    assert(!IsEofPending());
    assert(!verbatim || !eof_verbatim);

    had_input = true;

    const auto src_begin = (const char *)data0;
    const auto src_end = src_begin + length;

    auto src = src_begin;
    if (verbatim)
        /* skip the part that has already been parsed in the last
           invocation, but could not be consumed by the handler */
        src += pending_verbatim;

    while (src != src_end) {
        GError *error = nullptr;
        const ConstBuffer<char> src_remaining(src, src_end - src);
        auto data = ConstBuffer<char>::FromVoid(parser.Parse(src_remaining.ToVoid(),
                                                             &error));
        if (data.IsNull()) {
            Abort(error);
            return 0;
        }

        assert(data.data >= src);
        assert(data.data <= src_end);
        assert(data.end() <= src_end);

        src = data.data;

        if (!data.IsEmpty()) {
            assert(!parser.HasEnded());

            size_t nbytes;

            if (verbatim) {
                /* postpone this data chunk; try to send it all later in
                   one big block */
                nbytes = data.size;
            } else {
                had_output = true;
                seen_data += data.size;
                nbytes = InvokeData(src, data.size);
                assert(nbytes <= data.size);

                if (nbytes == 0) {
                    if (closed)
                        return 0;
                    else
                        break;
                }
            }

            src += nbytes;

            bool finished = parser.Consume(nbytes);
            if (!finished && !verbatim)
                break;
        } else if (parser.HasEnded()) {
            break;
        } else {
            assert(src == src_end);
        }
    }

    const size_t position = src - src_begin;
    if (verbatim && position > 0) {
        /* send all chunks in one big block */
        had_output = true;
        size_t nbytes = InvokeData(src_begin, position);
        if (closed)
            return 0;

        /* postpone the rest that was not handled; it will not be
           parsed again */
        pending_verbatim = position - nbytes;
        if (parser.HasEnded()) {
            if (pending_verbatim > 0)
                /* not everything could be sent; postpone to
                   next call */
                eof_verbatim = true;
            else if (!EofDetected())
                return 0;
        }

        return nbytes;
    } else if (parser.HasEnded()) {
        return EofDetected()
            ? position
            : 0;
    }

    if (!verbatim && !CalculateRemainingDataSize(src, src_end))
        return 0;

    return position;
}


/*
 * istream handler
 *
 */

size_t
DechunkIstream::OnData(const void *data, size_t length)
{
    assert(!verbatim || length >= pending_verbatim);

    if (IsEofPending())
        /* don't accept any more data after the EOF chunk */
        return 0;

    if (verbatim && eof_verbatim) {
        /* during the last call, the EOF chunk was parsed, but we
           could not handle it yet, because the handler did not
           consume all data yet; try to send the remaining pre-EOF
           data again and then handle the EOF chunk */

        assert(pending_verbatim > 0);

        assert(length >= pending_verbatim);

        had_output = true;
        size_t nbytes = InvokeData(data, pending_verbatim);
        if (nbytes == 0)
            return 0;

        pending_verbatim -= nbytes;
        if (pending_verbatim == 0 && !EofDetected())
            return 0;

        return nbytes;
    }

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return Feed(data, length);
}

void
DechunkIstream::OnEof()
{
    assert(!closed);

    input.Clear();

    if (IsEofPending())
        /* let defer_eof_event handle this */
        return;

    closed = true;

    if (eof)
        return;

    GError *error =
        g_error_new_literal(dechunk_quark(), 0,
                            "premature EOF in dechunker");
    DestroyError(error);
}

void
DechunkIstream::OnError(GError *error)
{
    input.Clear();

    if (IsEofPending()) {
        /* let defer_eof_event handle this */
        g_error_free(error);
        return;
    }

    assert(!closed);
    closed = true;
    DestroyError(error);
}

/*
 * istream implementation
 *
 */

off_t
DechunkIstream::_GetAvailable(bool partial)
{
    if (IsEofPending())
        return 0;

    if (verbatim) {
        if (!partial && !eof_verbatim)
            return -1;

        return pending_verbatim;
    } else {
        if (!partial && !seen_eof)
            return -1;

        return seen_data;
    }
}

void
DechunkIstream::_Read()
{
    if (IsEofPending())
        return;

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    had_output = false;

    do {
        had_input = false;
        input.Read();
    } while (input.IsDefined() && had_input && !had_output &&
             !IsEofPending());
}

void
DechunkIstream::_Close()
{
    assert(!eof);
    assert(!closed);

    closed = true;
    defer_eof_event.Cancel();

    if (input.IsDefined())
        input.ClearAndClose();
    Destroy();
}

/*
 * constructor
 *
 */

Istream *
istream_dechunk_new(struct pool *pool, Istream &input,
                    DechunkHandler &dechunk_handler)
{
    return NewIstream<DechunkIstream>(*pool, input, dechunk_handler);
}

bool
istream_dechunk_check_verbatim(Istream &i)
{
    auto *dechunk = DechunkIstream::CheckCast(i);
    if (dechunk == nullptr)
        /* not a DechunkIstream instance */
        return false;

    dechunk->SetVerbatim();
    return true;
}
