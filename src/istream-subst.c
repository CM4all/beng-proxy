/*
 * This istream filter substitutes a word with another string.
 *
 * Bug: the first character of the search word must not be present a
 * second time, because backtracking is not implemented.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>
#include <string.h>

struct istream_subst {
    struct istream output;
    istream_t input;
    const char *a, *b;
    size_t a_length, b_length;
    enum {
        /** searching the first matching character */
        STATE_NONE,

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

static void
subst_close(struct istream_subst *subst)
{
    if (subst->input != NULL)
        istream_free_unref(&subst->input);

    subst->a = NULL;

    istream_invoke_free(&subst->output);
}

/** write data from subst->b */
static size_t
subst_try_write_b(struct istream_subst *subst)
{
    size_t length, nbytes;

    assert(subst->state == STATE_INSERT);
    assert(subst->a_match == subst->a_length);

    length = subst->b_length - subst->b_sent;
    if (length == 0) {
        subst->state = STATE_NONE;
        return 0;
    }

    nbytes = istream_invoke_data(&subst->output, subst->b + subst->b_sent, length);
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

    nbytes = istream_invoke_data(&subst->output, subst->a + subst->a_sent, length);
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
    const char *data = _data, *p = data, *end = p + length, *first = NULL;
    size_t max_compare, chunk_length, nbytes, total = 0;

    assert(subst->input != NULL);
    assert(subst->a != NULL);

    /* find new match */

    do {
        switch (subst->state) {
        case STATE_NONE:
            /* find matching first char */

            first = memchr(p, subst->a[0], end - p);
            if (first == NULL)
                /* no match, try to write and return */
                return total + istream_invoke_data(&subst->output, data, end - data);

            subst->state = STATE_MATCH;
            subst->a_match = 1;

            p = first + 1;
            break;

        case STATE_MATCH:
            /* now see if the rest matches; note that max_compare may be
               0, but that isn't a problem */

            max_compare = subst->a_length - subst->a_match;
            if ((size_t)(end - p) < max_compare)
                max_compare = (size_t)(end - p);

            if (memcmp(p, subst->a + subst->a_match, max_compare) == 0) {
                /* all data (which is available) matches */

                subst->a_match += max_compare;
                p += max_compare;

                if (subst->a_match == subst->a_length) {
                    /* full match */

                    if (first != NULL && first > data) {
                        /* write the data chunk before the match */

                        chunk_length = first - data;
                        nbytes = istream_invoke_data(&subst->output, data, chunk_length);
                        if (subst->a == NULL)
                            return 0;

                        total += nbytes;
                        if (nbytes < chunk_length) {
                            /* blocking */
                            subst->state = STATE_NONE;
                            return total;
                        }
                    }

                    /* move data pointer */

                    data = p;
                    first = NULL;

                    /* switch state */

                    subst->state = STATE_INSERT;
                    subst->b_sent = 0;

                    /* consume substituted input */

                    total += subst->a_length;
                }
            } else {
                /* mismatch. reset match indicator and find new one */

                if (first != NULL && first > data) {
                    /* write the data chunk before the (mis-)match */

                    chunk_length = first - data;
                    nbytes = istream_invoke_data(&subst->output, data, chunk_length);
                    if (subst->a == NULL)
                        return 0;

                    total += nbytes;
                    if (nbytes < chunk_length) {
                        /* blocking */
                        subst->state = STATE_NONE;
                        return total;
                    }
                }

                /* move data pointer */

                data = p;
                first = NULL;

                /* switch state */

                subst->state = STATE_MISMATCH;
                subst->a_sent = 0;

                /* consume matched input */

                total += subst->a_match;
            }

            break;

        case STATE_INSERT:
            /* there is a previous full match, copy data from subst->b */

            subst_try_write_b(subst);
            if (subst->a == NULL)
                return 0;

            if (subst->state == STATE_INSERT)
                /* blocking */
                return total;

            break;

        case STATE_MISMATCH:
            /* there is a partial match following a mismatched character:
               backtrack and copy data from the beginning of subst->a */

            subst_try_write_a(subst);
            if (subst->a == NULL)
                return 0;

            if (subst->state == STATE_MISMATCH)
                /* blocking */
                return total;

            break;
        }
    } while (p < end || subst->state == STATE_INSERT ||
             subst->state == STATE_MISMATCH);

    if (first == NULL)
        /* we have found a partial match which we discard now, instead
           we will write the chunk right before this match */
        chunk_length = first - data;
    else
        /* there was no match (maybe a partial match which mismatched
           at a later stage): pass everything */
        chunk_length = end - data;

    if (chunk_length > 0) {
        /* discard possible match, expect the caller to give us the
           same possibly matching data chunk later */

        subst->state = STATE_NONE;
        subst->a_match = 0;

        /* write chunk */

        total += istream_invoke_data(&subst->output, data, chunk_length);
    }

    return total;
}

static void
subst_source_eof(void *ctx)
{
    struct istream_subst *subst = ctx;

    assert(subst->input != NULL);
    assert(subst->a != NULL);

    istream_clear_unref_handler(&subst->input);

    switch (subst->state) {
    case STATE_NONE:
        break;

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

    if (subst->a != NULL && subst->state == STATE_NONE)
        istream_invoke_eof(&subst->output);
}

static void
subst_source_free(void *ctx)
{
    struct istream_subst *subst = ctx;

    if (subst->input != NULL)
        istream_clear_unref(&subst->input);

    subst_close(subst);
}

static const struct istream_handler subst_source_handler = {
    .data = subst_source_data,
    .eof = subst_source_eof,
    .free = subst_source_free,
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

        istream_read(subst->input);
        return;

    case STATE_MISMATCH:
        subst_try_write_a(subst);
        break;

    case STATE_INSERT:
        subst_try_write_b(subst);
        break;
    }

    if (subst->a != NULL && subst->state == STATE_NONE)
        istream_invoke_eof(&subst->output);
}

static void
istream_subst_close(istream_t istream)
{
    struct istream_subst *subst = istream_to_subst(istream);

    subst_close(subst);
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
    subst->a = a;
    subst->b = b;
    subst->a_length = strlen(a);
    subst->b_length = strlen(b);
    subst->a_match = 0;
    subst->state = STATE_NONE;

    assert(subst->a_length > 0);

    istream_assign_ref_handler(&subst->input, input,
                               &subst_source_handler, subst,
                               0);

    return istream_struct_cast(&subst->output);
}
