/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_tee.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>

struct TeeIstream {
    struct {
        struct istream istream;

        /**
         * A weak output is one which is closed automatically when all
         * "strong" outputs have been closed - it will not keep up the
         * istream_tee object alone.
         */
        bool weak;

        bool enabled;
    } outputs[2];

    struct istream *input;

    /**
     * These flags control whether istream_tee_close[12]() may restart
     * reading for the other output.
     */
    bool reading, in_data;

#ifndef NDEBUG
    bool closed_while_reading, closed_while_data;
#endif

    /**
     * The number of bytes to skip for output 0.  The first output has
     * already consumed this many bytes, but the second output
     * blocked.
     */
    size_t skip;
};

static GQuark
tee_quark(void)
{
    return g_quark_from_static_string("tee");
}

static size_t
tee_feed0(TeeIstream &tee, const char *data, size_t length)
{
    if (!tee.outputs[0].enabled)
        return length;

    if (length <= tee.skip)
        /* all of this has already been sent to the first input, but
           the second one didn't accept it yet */
        return length;

    /* skip the part which was already sent */
    data += tee.skip;
    length -= tee.skip;

    size_t nbytes = istream_invoke_data(&tee.outputs[0].istream,
                                        data, length);
    if (nbytes > 0) {
        tee.skip += nbytes;
        return tee.skip;
    }

    if (tee.outputs[0].enabled || !tee.outputs[1].enabled)
        /* first output is blocking, or both closed: give up */
        return 0;

    /* the first output has been closed inside the data() callback,
       but the second is still alive: continue with the second
       output */
    return length;
}

static size_t
tee_feed1(TeeIstream &tee, const void *data, size_t length)
{
    if (!tee.outputs[1].enabled)
        return length;

    size_t nbytes = istream_invoke_data(&tee.outputs[1].istream, data, length);
    if (nbytes == 0 && !tee.outputs[1].enabled &&
        tee.outputs[0].enabled)
        /* during the data callback, outputs[1] has been closed,
           but outputs[0] continues; instead of returning 0 here,
           use outputs[0]'s result */
        return length;

    return nbytes;
}

static size_t
tee_feed(TeeIstream &tee, const void *data, size_t length)
{
    size_t nbytes0 = tee_feed0(tee, (const char *)data, length);
    if (nbytes0 == 0)
        return 0;

    size_t nbytes1 = tee_feed1(tee, data, nbytes0);
    if (nbytes1 > 0 && tee.outputs[0].enabled) {
        assert(nbytes1 <= tee.skip);
        tee.skip -= nbytes1;
    }

    return nbytes1;
}


/*
 * istream handler
 *
 */

static size_t
tee_input_data(const void *data, size_t length, void *ctx)
{
    TeeIstream &tee = *(TeeIstream *)ctx;

    assert(!tee.in_data);

    pool_ref(tee.outputs[0].istream.pool);
    tee.in_data = true;

    size_t nbytes = tee_feed(tee, data, length);

    tee.in_data = false;
    pool_unref(tee.outputs[0].istream.pool);

    return nbytes;
}

static void
tee_input_eof(void *ctx)
{
    TeeIstream &tee = *(TeeIstream *)ctx;

    assert(tee.input != nullptr);

    pool_ref(tee.outputs[0].istream.pool);

    tee.input = nullptr;

    /* clean up in reverse order */

    if (tee.outputs[1].enabled) {
        tee.outputs[1].enabled = false;
        istream_deinit_eof(&tee.outputs[1].istream);
    }

    if (tee.outputs[0].enabled) {
        tee.outputs[0].enabled = false;
        istream_deinit_eof(&tee.outputs[0].istream);
    }

    pool_unref(tee.outputs[0].istream.pool);
}

static void
tee_input_abort(GError *error, void *ctx)
{
    TeeIstream &tee = *(TeeIstream *)ctx;

    assert(tee.input != nullptr);

    pool_ref(tee.outputs[0].istream.pool);

    tee.input = nullptr;

    /* clean up in reverse order */

    if (tee.outputs[1].enabled) {
        tee.outputs[1].enabled = false;
        istream_deinit_abort(&tee.outputs[1].istream, g_error_copy(error));
    }

    if (tee.outputs[0].enabled) {
        tee.outputs[0].enabled = false;
        istream_deinit_abort(&tee.outputs[0].istream, g_error_copy(error));
    }

    g_error_free(error);

    pool_unref(tee.outputs[0].istream.pool);
}

static const struct istream_handler tee_input_handler = {
    .data = tee_input_data,
    /* .direct = tee_input_direct, XXX implement that using sys_tee() */
    .eof = tee_input_eof,
    .abort = tee_input_abort,
};


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
    return *ContainerCast(istream, TeeIstream, outputs[0].istream);
}

static off_t
istream_tee_available0(struct istream *istream, bool partial)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.outputs[0].enabled);

    return istream_available(tee.input, partial);
}

static void
istream_tee_read0(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.outputs[0].enabled);
    assert(!tee.reading);

    pool_ref(tee.outputs[0].istream.pool);

    tee.reading = true;
    istream_read(tee.input);
    tee.reading = false;

    pool_unref(tee.outputs[0].istream.pool);
}

static void
istream_tee_close0(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    assert(tee.outputs[0].enabled);

    tee.outputs[0].enabled = false;

#ifndef NDEBUG
    if (tee.reading)
        tee.closed_while_reading = true;
    if (tee.in_data)
        tee.closed_while_data = true;
#endif

    if (tee.input != nullptr) {
        if (!tee.outputs[1].enabled)
            istream_free_handler(&tee.input);
        else if (tee.outputs[1].weak) {
            pool_ref(tee.outputs[0].istream.pool);

            istream_free_handler(&tee.input);

            if (tee.outputs[1].enabled) {
                tee.outputs[1].enabled = false;

                GError *error =
                    g_error_new_literal(tee_quark(), 0,
                                        "closing the weak second output");
                istream_deinit_abort(&tee.outputs[1].istream, error);
            }

            pool_unref(tee.outputs[0].istream.pool);
        }
    }

    if (tee.input != nullptr && tee.outputs[1].enabled &&
        istream_has_handler(&tee.outputs[1].istream) &&
        !tee.in_data && !tee.reading)
        istream_read(tee.input);

    istream_deinit(&tee.outputs[0].istream);
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
    return *ContainerCast(istream, TeeIstream, outputs[1].istream);
}

static off_t
istream_tee_available1(struct istream *istream, bool partial)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(tee.outputs[1].enabled);

    return istream_available(tee.input, partial);
}

static void
istream_tee_read1(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(!tee.reading);

    pool_ref(tee.outputs[1].istream.pool);

    tee.reading = true;
    istream_read(tee.input);
    tee.reading = false;

    pool_unref(tee.outputs[1].istream.pool);
}

static void
istream_tee_close1(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee1(istream);

    assert(tee.outputs[1].enabled);

    tee.outputs[1].enabled = false;

#ifndef NDEBUG
    if (tee.reading)
        tee.closed_while_reading = true;
    if (tee.in_data)
        tee.closed_while_data = true;
#endif

    if (tee.input != nullptr) {
        if (!tee.outputs[0].enabled)
            istream_free_handler(&tee.input);
        else if (tee.outputs[0].weak) {
            pool_ref(tee.outputs[0].istream.pool);

            istream_free_handler(&tee.input);

            if (tee.outputs[0].enabled) {
                tee.outputs[0].enabled = false;

                GError *error =
                    g_error_new_literal(tee_quark(), 0,
                                        "closing the weak first output");
                istream_deinit_abort(&tee.outputs[0].istream, error);
            }

            pool_unref(tee.outputs[0].istream.pool);
        }
    }

    if (tee.input != nullptr && tee.outputs[0].enabled &&
        istream_has_handler(&tee.outputs[0].istream) &&
        !tee.in_data && !tee.reading)
        istream_read(tee.input);

    istream_deinit(&tee.outputs[1].istream);
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
    TeeIstream *tee = (TeeIstream *)
        istream_new(pool, &istream_tee0, sizeof(*tee));

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    istream_init(&tee->outputs[1].istream, &istream_tee1, tee->outputs[0].istream.pool);

    tee->outputs[0].weak = first_weak;
    tee->outputs[0].enabled = true;
    tee->outputs[1].weak = second_weak;
    tee->outputs[1].enabled = true;

    istream_assign_handler(&tee->input, input,
                           &tee_input_handler, tee,
                           0);

    tee->reading = false;
    tee->in_data = false;
    tee->skip = 0;

#ifndef NDEBUG
    tee->closed_while_reading = tee->closed_while_data = false;
#endif

    return &tee->outputs[0].istream;
}

struct istream *
istream_tee_second(struct istream *istream)
{
    TeeIstream &tee = istream_to_tee0(istream);

    return &tee.outputs[1].istream;
}

