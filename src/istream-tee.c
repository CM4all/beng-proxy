/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>

struct istream_tee {
    struct istream output1, output2;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
tee_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_tee *tee = ctx;
    size_t nbytes1, nbytes2;

    nbytes1 = istream_invoke_data(&tee->output1, data, length);
    if (nbytes1 == 0)
        return 0;

    nbytes2 = istream_invoke_data(&tee->output2, data, nbytes1);

    /* XXX it is currently asserted that the second handler will
       always consume all data; later, buffering should probably be
       added */
    assert(nbytes2 == nbytes1);

    return nbytes2;
}

static void
tee_source_eof(void *ctx)
{
    struct istream_tee *tee = ctx;

    assert(tee->input != NULL);

    istream_clear_unref(&tee->input);
    istream_invoke_eof(&tee->output1);
    istream_invoke_eof(&tee->output2);
}

static void
tee_source_abort(void *ctx)
{
    struct istream_tee *tee = ctx;

    assert(tee->input != NULL);

    istream_clear_unref(&tee->input);
    istream_invoke_abort(&tee->output1);
    istream_invoke_abort(&tee->output2);
}

static const struct istream_handler tee_input_handler = {
    .data = tee_source_data,
    /* .direct = tee_source_direct, XXX implement that using sys_tee() */
    .eof = tee_source_eof,
    .abort = tee_source_abort,
};


/*
 * istream implementation 1
 *
 */

static inline struct istream_tee *
istream_to_tee1(istream_t istream)
{
    return (struct istream_tee *)(((char*)istream) - offsetof(struct istream_tee, output1));
}

static void
istream_tee_read1(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    istream_read(tee->input);
}

static void
istream_tee_close1(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    assert(tee->input != NULL);

    istream_free_unref_handler(&tee->input);
    istream_invoke_abort(&tee->output2);
    istream_invoke_abort(&tee->output1);
}

static const struct istream istream_tee1 = {
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
    return (struct istream_tee *)(((char*)istream) - offsetof(struct istream_tee, output2));
}

static void
istream_tee_read2(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee2(istream);

    istream_read(tee->input);
}

static void
istream_tee_close2(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee2(istream);

    assert(tee->input != NULL);

    istream_free_unref_handler(&tee->input);
    istream_invoke_abort(&tee->output2);
    istream_invoke_abort(&tee->output1);
}

static const struct istream istream_tee2 = {
    .read = istream_tee_read2,
    .close = istream_tee_close2,
};


/*
 * constructor
 *
 */

istream_t
istream_tee_new(pool_t pool, istream_t input)
{
    struct istream_tee *tee = p_malloc(pool, sizeof(*tee));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    tee->output1 = istream_tee1;
    tee->output1.pool = pool;

    tee->output2 = istream_tee2;
    tee->output2.pool = pool;

    istream_assign_ref_handler(&tee->input, input,
                               &tee_input_handler, tee,
                               0);

    return istream_struct_cast(&tee->output1);
}

istream_t
istream_tee_second(istream_t istream)
{
    struct istream_tee *tee = istream_to_tee1(istream);

    return istream_struct_cast(&tee->output2);
}

