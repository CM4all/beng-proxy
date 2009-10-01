/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_tee {
    struct {
        struct istream istream;
        bool enabled;
    } outputs[2];
    istream_t input;
    bool fragile;

    /**
     * These flags control whether istream_tee_close[12]() may restart
     * reading for the other output.
     */
    bool reading, in_data;
};


static size_t
tee_feed(struct istream_tee *tee, const void *data, size_t length)
{
    size_t nbytes1, nbytes2;

    if (tee->outputs[0].enabled) {
        nbytes1 = istream_invoke_data(&tee->outputs[0].istream, data, length);
        if (nbytes1 == 0)
            return 0;
    } else
        nbytes1 = length;

    if (tee->outputs[1].enabled) {
        nbytes2 = istream_invoke_data(&tee->outputs[1].istream, data, nbytes1);

        /* XXX it is currently asserted that the second handler will
           always consume all data; later, buffering should probably be
           added */
        assert(nbytes2 == nbytes1 || (nbytes2 == 0 && !tee->outputs[1].enabled));
    } else
        nbytes2 = nbytes1;

    return nbytes2;
}


/*
 * istream handler
 *
 */

static size_t
tee_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_tee *tee = ctx;
    size_t nbytes;

    assert(!tee->in_data);

    pool_ref(tee->outputs[0].istream.pool);
    tee->in_data = true;

    nbytes = tee_feed(tee, data, length);

    tee->in_data = false;
    pool_unref(tee->outputs[0].istream.pool);

    return nbytes;
}

static void
tee_input_eof(void *ctx)
{
    struct istream_tee *tee = ctx;

    assert(tee->input != NULL);

    pool_ref(tee->outputs[0].istream.pool);

    tee->input = NULL;

    if (tee->outputs[0].enabled) {
        tee->outputs[0].enabled = false;
        istream_deinit_eof(&tee->outputs[0].istream);
    }

    if (tee->outputs[1].enabled) {
        tee->outputs[1].enabled = false;
        istream_deinit_eof(&tee->outputs[1].istream);
    }

    pool_unref(tee->outputs[0].istream.pool);
}

static void
tee_input_abort(void *ctx)
{
    struct istream_tee *tee = ctx;

    assert(tee->input != NULL);

    pool_ref(tee->outputs[0].istream.pool);

    tee->input = NULL;

    if (tee->outputs[0].enabled) {
        tee->outputs[0].enabled = false;
        istream_deinit_abort(&tee->outputs[0].istream);
    }

    if (tee->outputs[1].enabled) {
        tee->outputs[1].enabled = false;
        istream_deinit_abort(&tee->outputs[1].istream);
    }

    pool_unref(tee->outputs[0].istream.pool);
}

static const struct istream_handler tee_input_handler = {
    .data = tee_input_data,
    /* .direct = tee_input_direct, XXX implement that using sys_tee() */
    .eof = tee_input_eof,
    .abort = tee_input_abort,
};


/*
 * istream implementation 1
 *
 */

static inline struct istream_tee *
istream_to_tee1(istream_t istream)
{
    return (struct istream_tee *)(((char*)istream) - offsetof(struct istream_tee, outputs[0].istream));
}

static off_t
istream_tee_available1(istream_t istream, bool partial)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    assert(tee->outputs[0].enabled);

    return istream_available(tee->input, partial);
}

static void
istream_tee_read1(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    assert(tee->outputs[0].enabled);
    assert(!tee->reading);

    pool_ref(tee->outputs[0].istream.pool);

    tee->reading = true;
    istream_read(tee->input);
    tee->reading = false;

    pool_unref(tee->outputs[0].istream.pool);
}

static void
istream_tee_close1(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    assert(tee->outputs[0].enabled);

    tee->outputs[0].enabled = false;

    if (tee->input != NULL) {
        if (!tee->outputs[1].enabled)
            istream_free_handler(&tee->input);
        else if (tee->fragile)
            istream_close(tee->input);
    }

    istream_invoke_abort(&tee->outputs[0].istream);

    if (tee->input != NULL && tee->outputs[1].enabled &&
        !tee->in_data && !tee->reading)
        istream_read(tee->input);

    istream_deinit(&tee->outputs[0].istream);
}

static const struct istream istream_tee1 = {
    .available = istream_tee_available1,
    .read = istream_tee_read1,
    .close = istream_tee_close1,
};


/*
 * istream implementation 2
 *
 */

static inline struct istream_tee *
istream_to_tee2(istream_t istream)
{
    return (struct istream_tee *)(((char*)istream) - offsetof(struct istream_tee, outputs[1].istream));
}

static off_t
istream_tee_available2(istream_t istream, bool partial)
{
    struct istream_tee *tee = istream_to_tee2(istream);

    assert(tee->outputs[1].enabled);

    return istream_available(tee->input, partial);
}

static void
istream_tee_read2(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee2(istream);

    assert(!tee->reading);

    pool_ref(tee->outputs[1].istream.pool);

    tee->reading = true;
    istream_read(tee->input);
    tee->reading = false;

    pool_unref(tee->outputs[1].istream.pool);
}

static void
istream_tee_close2(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee2(istream);

    assert(tee->outputs[1].enabled);

    tee->outputs[1].enabled = false;

    if (tee->input != NULL) {
        if (!tee->outputs[0].enabled)
            istream_free_handler(&tee->input);
        else if (tee->fragile)
            istream_close(tee->input);
    }

    istream_invoke_abort(&tee->outputs[1].istream);

    if (tee->input != NULL && tee->outputs[0].enabled &&
        !tee->in_data && !tee->reading)
        istream_read(tee->input);

    istream_deinit(&tee->outputs[1].istream);
}

static const struct istream istream_tee2 = {
    .available = istream_tee_available2,
    .read = istream_tee_read2,
    .close = istream_tee_close2,
};


/*
 * constructor
 *
 */

istream_t
istream_tee_new(pool_t pool, istream_t input, bool fragile)
{
    struct istream_tee *tee = (struct istream_tee *)
        istream_new(pool, &istream_tee1, sizeof(*tee));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_init(&tee->outputs[1].istream, &istream_tee2, tee->outputs[0].istream.pool);

    tee->outputs[0].enabled = true;
    tee->outputs[1].enabled = true;

    istream_assign_handler(&tee->input, input,
                           &tee_input_handler, tee,
                           0);

    tee->fragile = fragile;
    tee->reading = false;
    tee->in_data = false;

    return istream_struct_cast(&tee->outputs[0].istream);
}

istream_t
istream_tee_second(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    return istream_struct_cast(&tee->outputs[1].istream);
}

