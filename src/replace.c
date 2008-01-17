/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "growing-buffer.h"

#include <daemon/log.h>

#include <assert.h>

struct substitution {
    struct substitution *next;
    struct replace *replace;
    off_t start, end;
    istream_t istream;
};

struct replace {
    struct istream output;
    istream_t input;

    int had_input;

    int quiet, writing;
    struct growing_buffer *buffer;
    off_t source_length, position;

    struct substitution *first_substitution, **append_substitution_p;

    int read_locked;

#ifndef NDEBUG
    off_t last_substitution_end;
#endif
};

/** is this substitution object the last chunk in this stream, i.e. is
    there no source data following it? */
static inline int
substitution_is_tail(const struct substitution *s)
{
    assert(s != NULL);
    assert(s->replace != NULL);
    assert(s->end <= s->replace->source_length);

    return s->next == NULL && (s->replace->quiet ||
                               s->end == s->replace->source_length);
}

static void
replace_read(struct replace *replace);

/**
 * Activate the next substitution object after s.
 */
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

    if (substitution_is_tail(s)) {
        istream_invoke_eof(&replace->output);
        return;
    }

    /* don't recurse if we're being called
       replace_read_substitution() */
    if (!replace->read_locked)
        replace_read(replace);
}


/*
 * istream handler
 *
 */

static size_t
replace_substitution_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = s->replace;

    if (!replace->writing)
        return 0;

    assert(replace->quiet || replace->position <= s->start);
    assert(replace->first_substitution != NULL);
    assert(replace->first_substitution->start <= s->start);

    if (replace->first_substitution != s ||
        (!replace->quiet && replace->position < s->start))
        return 0;

    return istream_invoke_data(&replace->output, data, length);
}

static void
replace_substitution_eof(void *ctx)
{
    struct substitution *s = ctx;
    struct replace *replace = s->replace;

    istream_clear_unref(&s->istream);

    if (replace->first_substitution != s ||
        !replace->writing ||
        (!replace->quiet && replace->position < s->start))
        return;

    replace_to_next_substitution(replace, s);
}

static const struct istream_handler replace_substitution_handler = {
    .data = replace_substitution_data,
    .eof = replace_substitution_eof,

    /* XXX display error message on abort()? */
    .abort = replace_substitution_eof,
};


/*
 * destructor
 *
 */

static void
replace_destroy(struct replace *replace)
{
    assert(replace != NULL);

    while (replace->first_substitution != NULL) {
        struct substitution *s = replace->first_substitution;
        replace->first_substitution = s->next;

        if (s->istream != NULL)
            istream_free_unref_handler(&s->istream);
    }
}


/*
 * misc methods
 *
 */

static size_t
replace_feed(struct replace *replace, const void *data, size_t length)
{
    assert(replace != NULL);
    assert(data != NULL);
    assert(length > 0);

    if (!replace->quiet) {
        if (replace->source_length >= 8 * 1024 * 1024) {
            daemon_log(2, "file too large for processor\n");
            istream_close(replace->input);
            return 0;
        }

        growing_buffer_write_buffer(replace->buffer, data, length);
    }

    replace->source_length += (off_t)length;

    return length;
}

static void
replace_add(struct replace *replace, off_t start, off_t end,
            istream_t istream)
{
    struct substitution *s;

    assert(replace != NULL);
    assert(replace->quiet || !replace->writing);
    assert(start >= 0);
    assert(start <= end);
    assert(start >= replace->last_substitution_end);

    s = p_malloc(replace->output.pool, sizeof(*s));
    s->next = NULL;
    s->replace = replace;
    s->start = start;
    s->end = end;

#ifndef NDEBUG
    replace->last_substitution_end = end;
#endif

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

static off_t
replace_available(const struct replace *replace)
{
    const struct substitution *subst;
    off_t length = 0, position = replace->position, l;

    for (subst = replace->first_substitution; subst != NULL; subst = subst->next) {
        assert(replace->quiet || position <= subst->start);

        if (!replace->quiet)
            length += subst->start - position;

        if (subst->istream != NULL) {
            l = istream_available(subst->istream, 1);
            if (l != (off_t)-1)
                length += l;
        }

        position = subst->end;
    }

    return length;
}

/**
 * Read data from substitution objects.
 */
static int
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
            return 1;
    }

    return 0;
}

/**
 * Copy data from the source buffer to the istream handler.
 *
 * @return 0 if the istream handler is not blocking; the number of
 * bytes remaining in the buffer if it is blocking
 */
static size_t
replace_read_from_buffer(struct replace *replace, size_t max_length)
{
    const void *data;
    size_t length, nbytes;

    assert(replace != NULL);
    assert(replace->buffer != NULL);
    assert(max_length > 0);

    data = growing_buffer_read(replace->buffer, &length);
    assert(data != NULL);
    assert(length > 0);

    if (length > max_length)
        length = max_length;

    nbytes = istream_invoke_data(&replace->output, data, length);
    assert(nbytes <= length);

    growing_buffer_consume(replace->buffer, nbytes);
    replace->position += nbytes;

    return length - nbytes;
}

/**
 * Copy the next chunk from the source buffer to the istream handler.
 *
 * @return 0 if the istream handler is not blocking; the number of
 * bytes remaining in the buffer if it is blocking
 */
static size_t
replace_try_read_from_buffer(struct replace *replace)
{
    size_t max_length, rest;

    assert(replace != NULL);

    if (replace->quiet)
        return 0;

    if (replace->first_substitution == NULL)
        max_length = (size_t)(replace->source_length - replace->position);
    else if (replace->position < replace->first_substitution->start)
        max_length = (size_t)(replace->first_substitution->start - replace->position);
    else
        max_length = 0;

    if (max_length == 0)
        return 0;

    rest = replace_read_from_buffer(replace, max_length);
    if (rest == 0 && replace->first_substitution == NULL)
        istream_invoke_eof(&replace->output);

    return rest;
}

static void
replace_read(struct replace *replace)
{
    pool_t pool;
    int blocking;
    size_t rest;

    assert(replace != NULL);
    assert(replace->quiet || replace->position <= replace->source_length);
    assert(replace->writing);

    pool_ref(replace->output.pool);
    pool = replace->output.pool;

    /* read until someone (input or output) blocks */
    do {
        blocking = replace_read_substitution(replace);
        if (blocking)
            break;

        rest = replace_try_read_from_buffer(replace);
        if (rest > 0)
            break;
    } while (replace->first_substitution != NULL);

    pool_unref(pool);
}

static void
replace_read_check_empty(struct replace *replace)
{
    assert(replace != NULL);
    assert(replace->writing);
    assert(replace->input == NULL);

    if (replace->quiet && replace->first_substitution == NULL)
        istream_invoke_eof(&replace->output);
    else
        replace_read(replace);
}


/*
 * input handler
 *
 */

static size_t
replace_source_data(const void *data, size_t length, void *ctx)
{
    struct replace *replace = ctx;

    replace->had_input = 1;

    return replace_feed(replace, data, length);
}

static void
replace_source_eof(void *ctx)
{
    struct replace *replace = ctx;

    istream_clear_unref(&replace->input);

    if (replace->writing)
        replace_read_check_empty(replace);
}

static void
replace_source_abort(void *ctx)
{
    struct replace *replace = ctx;

    replace_destroy(replace);
    istream_clear_unref(&replace->input);
    istream_invoke_abort(&replace->output);
}

static const struct istream_handler replace_input_handler = {
    .data = replace_source_data,
    .eof = replace_source_eof,
    .abort = replace_source_abort,
};


/*
 * istream implementation
 *
 */

static inline struct replace *
istream_to_replace(istream_t istream)
{
    return (struct replace *)(((char*)istream) - offsetof(struct replace, output));
}

static off_t
istream_replace_available(istream_t istream, int partial)
{
    struct replace *replace = istream_to_replace(istream);

    /* XXX optimize */

    if (partial && replace->writing)
        return replace_available(replace);
    else
        return (off_t)-1;
}

static void
istream_replace_read(istream_t istream)
{
    struct replace *replace = istream_to_replace(istream);

    if (replace->input != NULL) {
        do {
            replace->had_input = 0;
            istream_read(replace->input);
        } while (replace->input != NULL && replace->had_input);
    } else if (replace->writing)
        replace_read(replace);
}

static void
istream_replace_close(istream_t istream)
{
    struct replace *replace = istream_to_replace(istream);

    replace_destroy(replace);

    if (replace->input == NULL)
        istream_invoke_abort(&replace->output);
    else
        istream_close(replace->input);
}

static const struct istream istream_replace = {
    .available = istream_replace_available,
    .read = istream_replace_read,
    .close = istream_replace_close,
};


/*
 * constructor
 *
 */

istream_t
istream_replace_new(pool_t pool, istream_t input, int quiet)
{
    struct replace *replace = p_malloc(pool, sizeof(*replace));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    replace->output = istream_replace;
    replace->output.pool = pool;

    istream_assign_ref_handler(&replace->input, input,
                               &replace_input_handler, replace,
                               0);

    replace->quiet = quiet;
    replace->writing = 0;
    replace->source_length = 0;

    if (!quiet)
        replace->buffer = growing_buffer_new(pool, 8192);

    replace->first_substitution = NULL;
    replace->append_substitution_p = &replace->first_substitution;
    replace->read_locked = 0;

#ifndef NDEBUG
    replace->last_substitution_end = 0;
#endif

    return istream_struct_cast(&replace->output);
}

void
istream_replace_add(istream_t istream, off_t start, off_t end,
                    istream_t contents)
{
    struct replace *replace = istream_to_replace(istream);

    replace_add(replace, start, end, contents);
}

void
istream_replace_finish(istream_t istream)
{
    struct replace *replace = istream_to_replace(istream);

    assert(!replace->writing);

    replace->writing = 1;
    replace->position = 0;

    if (replace->input == NULL)
        replace_read_check_empty(replace);
}
