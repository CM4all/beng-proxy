/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "replace.h"
#include "growing-buffer.h"

#include <assert.h>

struct substitution {
    struct substitution *next;
    struct replace *replace;
    off_t start, end;
    istream_t istream;
};

static void
replace_to_next_substitution(struct replace *replace, struct substitution *s)
{
    assert(replace->first_substitution == s);
    assert(replace->quiet || replace->position == s->start);
    assert(s->istream == NULL);
    assert(s->start <= s->end);

    if (!replace->quiet) {
        growing_buffer_consume(replace->buffer, s->end - s->start);
        replace->position = s->end;
    }

    replace->first_substitution = s->next;

    assert(replace->quiet ||
           replace->first_substitution == NULL ||
           replace->first_substitution->start >= replace->position);

    if (!replace->read_locked)
        replace_read(replace);
}

static size_t
replace_substitution_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = s->replace;

    if (replace->reading_source)
        return 0;

    assert(replace->quiet || replace->position <= s->start);
    assert(replace->first_substitution != NULL);
    assert(replace->first_substitution->start <= s->start);

    if (replace->first_substitution != s ||
        (!replace->quiet && replace->position < s->start))
        return 0;

    return istream_invoke_data(replace->output, data, length);
}

static void
replace_substitution_free(void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = s->replace;

    istream_clear_unref(&s->istream);

    if (replace->first_substitution != s ||
        replace->reading_source ||
        (!replace->quiet && replace->position < s->start))
        return;

    replace_to_next_substitution(replace, s);
}

static const struct istream_handler replace_substitution_handler = {
    .data = replace_substitution_data,
    .free = replace_substitution_free,
};


void
replace_init(struct replace *replace, pool_t pool,
             struct istream *output,
             int quiet)
{
    assert(replace != NULL);

    replace->pool = pool;
    replace->output = output;

    replace->quiet = quiet;
    replace->reading_source = 1;
    if (!quiet) {
        replace->source_length = 0;
        replace->buffer = growing_buffer_new(pool, 8192);
    }

    replace->first_substitution = NULL;
    replace->append_substitution_p = &replace->first_substitution;
    replace->read_locked = 0;
}

void
replace_destroy(struct replace *replace)
{
    assert(replace != NULL);

    while (replace->first_substitution != NULL) {
        struct substitution *s = replace->first_substitution;
        replace->first_substitution = s->next;

        if (s->istream != NULL) {
            istream_close(s->istream);
            assert(s->istream == NULL);
        }
    }

    replace->quiet = 0;

    if (replace->output != NULL)
        istream_free((istream_t*)&replace->output); /* XXX */
}

size_t
replace_feed(struct replace *replace, const void *data, size_t length)
{
    assert(replace != NULL);
    assert(data != NULL);
    assert(length > 0);

    if (replace->quiet)
        return length;

    assert(replace->reading_source);

    growing_buffer_write_buffer(replace->buffer, data, length);
    replace->source_length += (off_t)length;

    return length;
}

void
replace_eof(struct replace *replace)
{
    assert(replace != NULL);

    if (!replace->quiet) {
        replace->reading_source = 0;
        replace->position = 0;
    }

    replace_read(replace);
}

void
replace_add(struct replace *replace, off_t start, off_t end,
            istream_t istream)
{
    struct substitution *s;

    assert(replace != NULL);
    assert(replace->quiet || replace->reading_source);
    assert(start >= 0);
    assert(start <= end);
    assert(replace->quiet || end <= replace->source_length);

    s = p_malloc(replace->pool, sizeof(*s));
    s->next = NULL;
    s->replace = replace;
    s->start = start;
    s->end = end;

    if (istream != NULL) {
        istream_assign_ref_handler(&s->istream, istream,
                                   &replace_substitution_handler, s,
                                   0);
    } else {
        s->istream = NULL;
    }

    *replace->append_substitution_p = s;
    replace->append_substitution_p = &s->next;
}

static void
replace_read_substitution(struct replace *replace)
{
    while (replace->first_substitution != NULL &&
           (replace->quiet ||
            replace->position == replace->first_substitution->start)) {
        struct substitution *s = replace->first_substitution;

        replace->read_locked = 1;

        if (s->istream == NULL)
            replace_to_next_substitution(replace, s);
        else
            istream_read(s->istream);

        replace->read_locked = 0;

        /* we assume the substitution object is blocking if it hasn't
           reached EOF with this one call */
        if (s == replace->first_substitution)
            return;
    }
}

void
replace_read(struct replace *replace)
{
    pool_t pool;
    size_t rest, nbytes;

    assert(replace != NULL);
    assert(replace->output != NULL);
    assert(replace->quiet || replace->position <= replace->source_length);

    if (replace->reading_source)
        return;

    pool_ref(replace->pool);
    pool = replace->pool;

    replace_read_substitution(replace);
    if (replace->output == NULL) {
        pool_unref(pool);
        return;
    }

    if (replace->quiet)
        rest = 0;
    else if (replace->first_substitution == NULL)
        rest = (size_t)(replace->source_length - replace->position);
    else if (replace->position < replace->first_substitution->start)
        rest = (size_t)(replace->first_substitution->start - replace->position);
    else
        rest = 0;

    if (replace->buffer != NULL && rest > 0) {
        const void *data;
        size_t length;

        data = growing_buffer_read(replace->buffer, &length);
        assert(data != NULL);
        assert(length > 0);

        if (length > rest)
            length = rest;

        nbytes = istream_invoke_data(replace->output, data, length);
        assert(nbytes <= length);

        growing_buffer_consume(replace->buffer, nbytes);
        replace->position += nbytes;
    }

    if (replace->first_substitution == NULL &&
        (replace->quiet ||
         (replace->buffer != NULL &&
          replace->position == replace->source_length))) {
        if (!replace->quiet)
            replace->buffer = NULL;

        pool_ref(replace->pool);

        istream_invoke_eof(replace->output);
        replace_destroy(replace);

        pool_unref(replace->pool);
    }

    pool_unref(replace->pool);
}
