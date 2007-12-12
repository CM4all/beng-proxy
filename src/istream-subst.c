/*
 * This istream filter substitutes a word with another string.
 *
 * Bug: the first character of the search word must not be present a
 * second time, because backtracking is not implemented.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "strref.h"

#include <assert.h>
#include <string.h>

struct istream_subst {
    struct istream output;
    istream_t input;
    unsigned had_input:1, had_output:1;

    struct strref a, b;

    enum {
        /** searching the first matching character */
        STATE_NONE,

        /** the istream has been closed */
        STATE_CLOSED,

        /** at least the first character was found, checking for the
            rest */
        STATE_MATCH,

        /** inserting the substitution */
        STATE_INSERT,

        /** inserting part of the original until we reach the
            mismatch */
        STATE_MISMATCH,
    } state;
    size_t a_match, a_sent, b_sent;
};


/*
 * helper functions
 *
 */

/** write data from subst->b */
static size_t
subst_try_write_b(struct istream_subst *subst)
{
    size_t length, nbytes;

    assert(subst->state == STATE_INSERT);
    assert(subst->a_match == subst->a.length);

    length = subst->b.length - subst->b_sent;
    if (length == 0) {
        subst->state = STATE_NONE;
        return 0;
    }

    nbytes = istream_invoke_data(&subst->output, subst->b.data + subst->b_sent, length);
    assert(nbytes <= length);

    /* note progress */
    subst->b_sent += nbytes;

    /* finished sending substitution? */
    if (nbytes == length)
        subst->state = STATE_NONE;

    return nbytes;
}

/** write data from subst->a (mismatch after partial match) */
static size_t
subst_try_write_a(struct istream_subst *subst)
{
    size_t length, nbytes;

    assert(subst->state == STATE_MISMATCH);
    assert(subst->a_match > 0);
    assert(subst->a_sent <= subst->a_match);

    length = subst->a_match - subst->a_sent;
    if (length == 0) {
        subst->state = STATE_NONE;
        return 0;
    }

    nbytes = istream_invoke_data(&subst->output, subst->a.data + subst->a_sent, length);
    assert(nbytes <= length);

    /* note progress */
    subst->a_sent += nbytes;

    /* finished sending substitution? */
    if (nbytes == length)
        subst->state = STATE_NONE;

    return nbytes;
}


/*
 * istream handler
 *
 */

static size_t
subst_source_data(const void *_data, size_t length, void *ctx)
{
    struct istream_subst *subst = ctx;
    const char *data0 = _data, *data = data0, *p = data0, *end = p + length, *first = NULL;
    size_t max_compare, chunk_length, nbytes;

    assert(subst->input != NULL);

    subst->had_input = 1;

    /* find new match */

    do {
        assert(data >= data0);
        assert(p >= data);
        assert(p <= end);

        switch (subst->state) {
        case STATE_NONE:
            /* find matching first char */

            assert(first == NULL);

            first = memchr(p, subst->a.data[0], end - p);
            if (first == NULL) {
                /* no match, try to write and return */
                subst->had_output = 1;
                nbytes = istream_invoke_data(&subst->output, data, end - data);
                if (nbytes == 0 && subst->state == STATE_CLOSED)
                    return 0;

                return (data - data0) + nbytes;
            }

            subst->state = STATE_MATCH;
            subst->a_match = 1;

            p = first + 1;
            break;

        case STATE_CLOSED:
            assert(0);

        case STATE_MATCH:
            /* now see if the rest matches; note that max_compare may be
               0, but that isn't a problem */

            max_compare = subst->a.length - subst->a_match;
            if ((size_t)(end - p) < max_compare)
                max_compare = (size_t)(end - p);

            if (memcmp(p, subst->a.data + subst->a_match, max_compare) == 0) {
                /* all data (which is available) matches */

                subst->a_match += max_compare;
                p += max_compare;

                if (subst->a_match == subst->a.length) {
                    /* full match */

                    if (first != NULL && first > data) {
                        /* write the data chunk before the match */

                        subst->had_output = 1;

                        chunk_length = first - data;
                        nbytes = istream_invoke_data(&subst->output, data, chunk_length);
                        if (nbytes == 0 && subst->state == STATE_CLOSED)
                            return 0;

                        if (nbytes < chunk_length) {
                            /* blocking */
                            subst->state = STATE_NONE;
                            return (data - data0) + nbytes;
                        }
                    }

                    /* move data pointer */

                    data = p;
                    first = NULL;

                    /* switch state */

                    subst->state = STATE_INSERT;
                    subst->b_sent = 0;
                }
            } else {
                /* mismatch. reset match indicator and find new one */

                if (first != NULL && first > data) {
                    /* write the data chunk before the (mis-)match */

                    subst->had_output = 1;

                    chunk_length = first - data;
                    nbytes = istream_invoke_data(&subst->output, data, chunk_length);
                    if (nbytes == 0 && subst->state == STATE_CLOSED)
                        return 0;

                    if (nbytes < chunk_length) {
                        /* blocking */
                        subst->state = STATE_NONE;
                        return (data - data0) + nbytes;
                    }
                }

                /* move data pointer */

                data = p;
                first = NULL;

                /* switch state */

                subst->state = STATE_MISMATCH;
                subst->a_sent = 0;
            }

            break;

        case STATE_INSERT:
            /* there is a previous full match, copy data from subst->b */

            subst_try_write_b(subst);

            if (subst->state == STATE_CLOSED)
                return 0;

            if (subst->state == STATE_INSERT)
                /* blocking */
                return data - data0;

            break;

        case STATE_MISMATCH:
            /* there is a partial match following a mismatched character:
               backtrack and copy data from the beginning of subst->a */

            subst_try_write_a(subst);

            if (subst->state == STATE_CLOSED)
                return 0;

            if (subst->state == STATE_MISMATCH)
                /* blocking */
                return data - data0;

            break;
        }
    } while (p < end || subst->state == STATE_INSERT ||
             subst->state == STATE_MISMATCH);

    if (first != NULL)
        /* we have found a partial match which we discard now, instead
           we will write the chunk right before this match */
        chunk_length = first - data;
    else if (subst->state == STATE_INSERT || subst->state == STATE_MISMATCH)
        chunk_length = 0;
    else
        /* there was no match (maybe a partial match which mismatched
           at a later stage): pass everything */
        chunk_length = end - data;

    if (chunk_length > 0) {
        /* write chunk */

        subst->had_output = 1;

        nbytes = istream_invoke_data(&subst->output, data, chunk_length);
        if (nbytes == 0 && subst->state == STATE_CLOSED)
            return 0;

        data += nbytes;

        if (nbytes < chunk_length) {
            /* discard match because our attempt to write the chunk
               before it blocked */
            subst->state = STATE_NONE;
            return (data - data0) + nbytes;
        }
    }

    return p - data0;
}

static void
subst_source_eof(void *ctx)
{
    struct istream_subst *subst = ctx;

    assert(subst->input != NULL);

    istream_clear_unref(&subst->input);

    switch (subst->state) {
    case STATE_NONE:
        break;

    case STATE_CLOSED:
        assert(0);

    case STATE_MATCH:
        /* we're in the middle of a match, technically making this a
           mismatch because we reach end of file before end of
           match */
        subst->state = STATE_MISMATCH;
        subst->a_sent = 0;
        /* intentionally no break */

    case STATE_MISMATCH:
        subst_try_write_a(subst);
        break;

    case STATE_INSERT:
        subst_try_write_b(subst);
        break;
    }

    if (subst->state == STATE_NONE) {
        subst->state = STATE_CLOSED;
        istream_invoke_eof(&subst->output);
    }
}

static void
subst_source_abort(void *ctx)
{
    struct istream_subst *subst = ctx;

    subst->state = STATE_CLOSED;

    istream_clear_unref(&subst->input);

    istream_invoke_abort(&subst->output);
}

static const struct istream_handler subst_source_handler = {
    .data = subst_source_data,
    .eof = subst_source_eof,
    .abort = subst_source_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_subst *
istream_to_subst(istream_t istream)
{
    return (struct istream_subst *)(((char*)istream) - offsetof(struct istream_subst, output));
}

static void
istream_subst_read(istream_t istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    switch (subst->state) {
    case STATE_NONE:
    case STATE_MATCH:
        assert(subst->input != NULL);

        subst->had_output = 0;

        do {
            subst->had_input = 0;
            istream_read(subst->input);
        } while (subst->input != NULL && subst->had_input &&
                 !subst->had_output);

        return;

    case STATE_CLOSED:
        assert(0);

    case STATE_MISMATCH:
        subst_try_write_a(subst);
        break;

    case STATE_INSERT:
        subst_try_write_b(subst);
        break;
    }

    if (subst->state == STATE_NONE && subst->input == NULL) {
        subst->state = STATE_CLOSED;
        istream_invoke_eof(&subst->output);
    }
}

static void
istream_subst_close(istream_t istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    subst->state = STATE_CLOSED;

    if (subst->input != NULL)
        istream_free_unref_handler(&subst->input);

    istream_invoke_abort(&subst->output);
}

static const struct istream istream_subst = {
    .read = istream_subst_read,
    .close = istream_subst_close,
};


/*
 * constructor
 *
 */

istream_t
istream_subst_new(pool_t pool, istream_t input,
                  const char *a, const char *b)
{
    struct istream_subst *subst = p_malloc(pool, sizeof(*subst));

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(a != NULL);
    assert(b != NULL);

    subst->output = istream_subst;
    subst->output.pool = pool;
    strref_set_c(&subst->a, a);
    strref_set_c(&subst->b, b);
    subst->state = STATE_NONE;

    assert(!strref_is_empty(&subst->a));

    istream_assign_ref_handler(&subst->input, input,
                               &subst_source_handler, subst,
                               0);

    return istream_struct_cast(&subst->output);
}
