/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_tee.hxx"
#include "istream_oo.hxx"
#include "istream_pointer.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <assert.h>

struct TeeIstream {
    struct Output {
        struct istream istream;

        /**
         * A weak output is one which is closed automatically when all
         * "strong" outputs have been closed - it will not keep up the
         * istream_tee object alone.
         */
        bool weak;

        bool enabled = true;

        explicit Output(bool _weak):weak(_weak) {}

        Output(const Output &) = delete;
        Output &operator=(const Output &) = delete;
    } first_output, second_output;

    IstreamPointer input;

    /**
     * These flags control whether istream_tee_close[12]() may restart
     * reading for the other output.
     */
    bool reading = false, in_data = false;

#ifndef NDEBUG
    bool closed_while_reading = false, closed_while_data = false;
#endif

    /**
     * The number of bytes to skip for output 0.  The first output has
     * already consumed this many bytes, but the second output
     * blocked.
     */
    size_t skip = 0;

    TeeIstream(struct istream &_input,
               bool first_weak, bool second_weak)
        :first_output(first_weak),
         second_output(second_weak),
         input(_input, MakeIstreamHandler<TeeIstream>::handler, this)
    {
    }

    size_t Feed0(const char *data, size_t length);
    size_t Feed1(const void *data, size_t length);
    size_t Feed(const void *data, size_t length);

    /* handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        // TODO: implement that using sys_tee()
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);
};

static GQuark
tee_quark(void)
{
    return g_quark_from_static_string("tee");
}

inline size_t
TeeIstream::Feed0(const char *data, size_t length)
{
    if (!first_output.enabled)
        return length;

    if (length <= skip)
        /* all of this has already been sent to the first input, but
           the second one didn't accept it yet */
        return length;

    /* skip the part which was already sent */
    data += skip;
    length -= skip;

    size_t nbytes = istream_invoke_data(&first_output.istream,
                                        data, length);
    if (nbytes > 0) {
        skip += nbytes;
        return skip;
    }

    if (first_output.enabled || !second_output.enabled)
        /* first output is blocking, or both closed: give up */
        return 0;

    /* the first output has been closed inside the data() callback,
       but the second is still alive: continue with the second
       output */
    return length;
}

inline size_t
TeeIstream::Feed1(const void *data, size_t length)
{
    if (!second_output.enabled)
        return length;

    size_t nbytes = istream_invoke_data(&second_output.istream, data, length);
    if (nbytes == 0 && !second_output.enabled &&
        first_output.enabled)
        /* during the data callback, second_output has been closed,
           but first_output continues; instead of returning 0 here,
           use first_output's result */
        return length;

    return nbytes;
}

inline size_t
TeeIstream::Feed(const void *data, size_t length)
{
    size_t nbytes0 = Feed0((const char *)data, length);
    if (nbytes0 == 0)
        return 0;

    size_t nbytes1 = Feed1(data, nbytes0);
    if (nbytes1 > 0 && first_output.enabled) {
        assert(nbytes1 <= skip);
        skip -= nbytes1;
    }

    return nbytes1;
}


/*
 * istream handler
 *
 */

inline size_t
TeeIstream::OnData(const void *data, size_t length)
{
    assert(input.IsDefined());
    assert(!in_data);

    const ScopePoolRef ref(*first_output.istream.pool TRACE_ARGS);
    in_data = true;
    size_t nbytes = Feed(data, length);
    in_data = false;
    return nbytes;
}

inline void
TeeIstream::OnEof()
{
    assert(input.IsDefined());
    input.Clear();

    const ScopePoolRef ref(*first_output.istream.pool TRACE_ARGS);

    /* clean up in reverse order */

    if (second_output.enabled) {
        second_output.enabled = false;
        istream_deinit_eof(&second_output.istream);
    }

    if (first_output.enabled) {
        first_output.enabled = false;
        istream_deinit_eof(&first_output.istream);
    }
}

inline void
TeeIstream::OnError(GError *error)
{
    assert(input.IsDefined());
    input.Clear();

    const ScopePoolRef ref(*first_output.istream.pool TRACE_ARGS);

    /* clean up in reverse order */

    if (second_output.enabled) {
        second_output.enabled = false;
        istream_deinit_abort(&second_output.istream, g_error_copy(error));
    }

    if (first_output.enabled) {
        first_output.enabled = false;
        istream_deinit_abort(&first_output.istream, g_error_copy(error));
    }

    g_error_free(error);
}

/*
 * istream implementation 0
 *
 */

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wextended-offsetof"
#endif

static inline TeeIstream &
istream_to_tee0(struct istream *istream)
{
    return *ContainerCast(istream, TeeIstream, first_output.istream);
}

static off_t
istream_tee_available0(struct istream *istream, bool partial)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.first_output.enabled);

    return tee.input.GetAvailable(partial);
}

static void
istream_tee_read0(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.first_output.enabled);
    assert(!tee.reading);

    const ScopePoolRef ref(*tee.first_output.istream.pool TRACE_ARGS);
    tee.reading = true;
    tee.input.Read();
    tee.reading = false;
}

static void
istream_tee_close0(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.first_output.enabled);

    tee.first_output.enabled = false;

#ifndef NDEBUG
    if (tee.reading)
        tee.closed_while_reading = true;
    if (tee.in_data)
        tee.closed_while_data = true;
#endif

    if (tee.input.IsDefined()) {
        if (!tee.second_output.enabled)
            tee.input.ClearAndClose();
        else if (tee.second_output.weak) {
            const ScopePoolRef ref(*tee.first_output.istream.pool TRACE_ARGS);

            tee.input.ClearAndClose();

            if (tee.second_output.enabled) {
                tee.second_output.enabled = false;

                GError *error =
                    g_error_new_literal(tee_quark(), 0,
                                        "closing the weak second output");
                istream_deinit_abort(&tee.second_output.istream, error);
            }
        }
    }

    if (tee.input.IsDefined() && tee.second_output.enabled &&
        istream_has_handler(&tee.second_output.istream) &&
        !tee.in_data && !tee.reading)
        tee.input.Read();

    istream_deinit(&tee.first_output.istream);
}

static const struct istream_class istream_tee0 = {
    .available = istream_tee_available0,
    .read = istream_tee_read0,
    .close = istream_tee_close0,
};


/*
 * istream implementation 2
 *
 */

static inline TeeIstream &
istream_to_tee1(struct istream *istream)
{
    return *ContainerCast(istream, TeeIstream, second_output.istream);
}

static off_t
istream_tee_available1(struct istream *istream, bool partial)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(tee.second_output.enabled);

    return tee.input.GetAvailable(partial);
}

static void
istream_tee_read1(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(!tee.reading);

    const ScopePoolRef ref(*tee.first_output.istream.pool TRACE_ARGS);
    tee.reading = true;
    tee.input.Read();
    tee.reading = false;
}

static void
istream_tee_close1(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(tee.second_output.enabled);

    tee.second_output.enabled = false;

#ifndef NDEBUG
    if (tee.reading)
        tee.closed_while_reading = true;
    if (tee.in_data)
        tee.closed_while_data = true;
#endif

    if (tee.input.IsDefined()) {
        if (!tee.first_output.enabled)
            tee.input.ClearAndClose();
        else if (tee.first_output.weak) {
            const ScopePoolRef ref(*tee.first_output.istream.pool TRACE_ARGS);

            tee.input.ClearAndClose();

            if (tee.first_output.enabled) {
                tee.first_output.enabled = false;

                GError *error =
                    g_error_new_literal(tee_quark(), 0,
                                        "closing the weak first output");
                istream_deinit_abort(&tee.first_output.istream, error);
            }
        }
    }

    if (tee.input.IsDefined() && tee.first_output.enabled &&
        istream_has_handler(&tee.first_output.istream) &&
        !tee.in_data && !tee.reading)
        tee.input.Read();

    istream_deinit(&tee.second_output.istream);
}

static const struct istream_class istream_tee1 = {
    .available = istream_tee_available1,
    .read = istream_tee_read1,
    .close = istream_tee_close1,
};


/*
 * constructor
 *
 */

struct istream *
istream_tee_new(struct pool *pool, struct istream *input,
                bool first_weak, bool second_weak)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto tee = NewFromPool<TeeIstream>(*pool, *input,
                                       first_weak, second_weak);
    istream_init(&tee->first_output.istream, &istream_tee0, pool);
    istream_init(&tee->second_output.istream, &istream_tee1, pool);

    return &tee->first_output.istream;
}

struct istream *
istream_tee_second(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    return &tee.second_output.istream;
}

