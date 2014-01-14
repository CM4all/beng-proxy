/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-replace.h"
#include "istream-internal.h"
#include "growing-buffer.h"

#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>

struct substitution {
    struct substitution *next;
    struct istream_replace *replace;
    off_t start, end;
    struct istream *istream;
};

struct istream_replace {
    struct istream output;
    struct istream *input;

    bool finished, read_locked;
    bool had_input, had_output;

    struct growing_buffer *buffer;
    off_t source_length, position;

    /**
     * The offset given by istream_replace_settle() or the end offset
     * of the last substitution (whichever is bigger).
     */
    off_t settled_position;

    struct growing_buffer_reader reader;

    struct substitution *first_substitution, **append_substitution_p;

#ifndef NDEBUG
    off_t last_substitution_end;
#endif
};

static GQuark
replace_quark(void)
{
    return g_quark_from_static_string("replace");
}

/**
 * Is the buffer at the end-of-file position?
 */
static inline bool
replace_buffer_eof(const struct istream_replace *replace)
{
    return replace->position == replace->source_length;
}

/**
 * Is the object at end-of-file?
 */
static inline bool
replace_is_eof(const struct istream_replace *replace)
{
    return replace->input == NULL && replace->finished &&
        replace->first_substitution == NULL &&
        replace_buffer_eof(replace);
}

/**
 * Is this substitution object is active, i.e. its data is the next
 * being written?
 */
static inline bool
substitution_is_active(const struct substitution *s)
{
    const struct istream_replace *replace = s->replace;

    assert(replace != NULL);
    assert(replace->first_substitution != NULL);
    assert(replace->first_substitution->start <= s->start);
    assert(s->start >= replace->position);

    return s == replace->first_substitution &&
        replace->position == s->start;
}

static void
replace_read(struct istream_replace *replace);

/**
 * Activate the next substitution object after s.
 */
static void
replace_to_next_substitution(struct istream_replace *replace, struct substitution *s)
{
    assert(replace->first_substitution == s);
    assert(substitution_is_active(s));
    assert(s->istream == NULL);
    assert(s->start <= s->end);

    growing_buffer_reader_skip(&replace->reader, s->end - s->start);
    replace->position = s->end;

    replace->first_substitution = s->next;
    if (replace->first_substitution == NULL) {
        assert(replace->append_substitution_p == &s->next);
        replace->append_substitution_p = &replace->first_substitution;
    }

    p_free(replace->output.pool, s);

    assert(replace->first_substitution == NULL ||
           replace->first_substitution->start >= replace->position);

    if (replace_is_eof(replace)) {
        istream_deinit_eof(&replace->output);
        return;
    }

    /* don't recurse if we're being called
       replace_read_substitution() */
    if (!replace->read_locked) {
        pool_ref(replace->output.pool);
        replace_read(replace);
        pool_unref(replace->output.pool);
    }
}

static void
replace_destroy(struct istream_replace *replace);


/*
 * istream handler
 *
 */

static size_t
replace_substitution_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;
    struct istream_replace *replace = s->replace;

    if (substitution_is_active(s)) {
        replace->had_output = true;
        return istream_invoke_data(&replace->output, data, length);
    } else
        return 0;
}

static void
replace_substitution_eof(void *ctx)
{
    struct substitution *s = ctx;
    struct istream_replace *replace = s->replace;

    s->istream = NULL;

    if (substitution_is_active(s))
        replace_to_next_substitution(replace, s);
}

static void
replace_substitution_abort(GError *error, void *ctx)
{
    struct substitution *s = ctx;
    struct istream_replace *replace = s->replace;

    s->istream = NULL;

    replace_destroy(replace);

    if (replace->input != NULL)
        istream_free_handler(&replace->input);

    istream_deinit_abort(&replace->output, error);
}

static const struct istream_handler replace_substitution_handler = {
    .data = replace_substitution_data,
    .eof = replace_substitution_eof,
    .abort = replace_substitution_abort,
};


/*
 * destructor
 *
 */

static void
replace_destroy(struct istream_replace *replace)
{
    assert(replace != NULL);
    assert(replace->source_length != (off_t)-1);

    /* source_length -1 is the "destroyed" marker */
    replace->source_length = (off_t)-1;

    while (replace->first_substitution != NULL) {
        struct substitution *s = replace->first_substitution;
        replace->first_substitution = s->next;

        if (s->istream != NULL)
            istream_free_handler(&s->istream);
    }
}


/*
 * misc methods
 *
 */

/**
 * Read data from substitution objects.
 */
static bool
replace_read_substitution(struct istream_replace *replace)
{
    while (replace->first_substitution != NULL &&
           substitution_is_active(replace->first_substitution)) {
        struct substitution *s = replace->first_substitution;

        replace->read_locked = true;

        if (s->istream == NULL)
            replace_to_next_substitution(replace, s);
        else
            istream_read(s->istream);

        replace->read_locked = false;

        /* we assume the substitution object is blocking if it hasn't
           reached EOF with this one call */
        if (s == replace->first_substitution)
            return true;
    }

    return false;
}

/**
 * Copy data from the source buffer to the istream handler.
 *
 * @return 0 if the istream handler is not blocking; the number of
 * bytes remaining in the buffer if it is blocking
 */
static size_t
replace_read_from_buffer(struct istream_replace *replace, size_t max_length)
{
    const void *data;
    size_t length, nbytes;

    assert(replace != NULL);
    assert(max_length > 0);

    data = growing_buffer_reader_read(&replace->reader, &length);
    assert(data != NULL);
    assert(length > 0);

    if (length > max_length)
        length = max_length;

    replace->had_output = true;
    nbytes = istream_invoke_data(&replace->output, data, length);
    assert(nbytes <= length);

    if (nbytes == 0)
        /* istream_replace has been closed */
        return length;

    growing_buffer_reader_consume(&replace->reader, nbytes);
    replace->position += nbytes;

    assert(replace->position <= replace->source_length);

    return length - nbytes;
}

static size_t
replace_read_from_buffer_loop(struct istream_replace *replace, off_t end)
{
    size_t max_length, rest;
#ifndef NDEBUG
    struct pool_notify_state notify;
#endif

    assert(replace != NULL);
    assert(end > replace->position);
    assert(end <= replace->source_length);

    /* this loop is required to cross the growing_buffer borders */
    do {
#ifndef NDEBUG
        pool_notify(replace->output.pool, &notify);
#endif

        max_length = (size_t)(end - replace->position);
        rest = replace_read_from_buffer(replace, max_length);

#ifndef NDEBUG
        if (pool_denotify(&notify)) {
            assert(rest > 0);
            break;
        }
#endif

        assert(replace->position <= end);
    } while (rest == 0 && replace->position < end);

    return rest;
}

/**
 * Copy the next chunk from the source buffer to the istream handler.
 *
 * @return 0 if the istream handler is not blocking; the number of
 * bytes remaining in the buffer if it is blocking
 */
static size_t
replace_try_read_from_buffer(struct istream_replace *replace)
{
    off_t end;
    size_t rest;

    assert(replace != NULL);

    if (replace->first_substitution == NULL) {
        if (replace->finished)
            end = replace->source_length;
        else if (replace->position < replace->settled_position)
            end = replace->settled_position;
        else
            /* block after the last substitution, unless the caller
               has already set the "finished" flag */
            return 1;

        assert(replace->position < replace->source_length);
    } else {
        end = replace->first_substitution->start;
        assert(end >= replace->position);

        if (end == replace->position)
            return 0;
    }

    rest = replace_read_from_buffer_loop(replace, end);
    if (rest == 0 && replace->position == replace->source_length &&
        replace->first_substitution == NULL &&
        replace->input == NULL)
        istream_deinit_eof(&replace->output);

    return rest;
}

static void
replace_read(struct istream_replace *replace)
{
    bool blocking;
    size_t rest;

    assert(replace != NULL);
    assert(replace->position <= replace->source_length);

    /* read until someone (input or output) blocks */
    do {
        blocking = replace_read_substitution(replace);
        if (blocking || replace_buffer_eof(replace) ||
            replace->source_length == (off_t)-1)
            break;

        rest = replace_try_read_from_buffer(replace);
    } while (rest == 0 && replace->first_substitution != NULL);
}

static void
replace_read_check_empty(struct istream_replace *replace)
{
    assert(replace != NULL);
    assert(replace->finished);
    assert(replace->input == NULL);

    if (replace_is_eof(replace))
        istream_deinit_eof(&replace->output);
    else {
        pool_ref(replace->output.pool);
        replace_read(replace);
        pool_unref(replace->output.pool);
    }
}


/*
 * input handler
 *
 */

static size_t
replace_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_replace *replace = ctx;

    replace->had_input = true;

    if (replace->source_length >= 8 * 1024 * 1024) {
        istream_free_handler(&replace->input);
        replace_destroy(replace);

        GError *error =
            g_error_new_literal(replace_quark(), 0,
                                "file too large for processor");
        istream_deinit_abort(&replace->output, error);
        return 0;
    }

    growing_buffer_write_buffer(replace->buffer, data, length);
    replace->source_length += (off_t)length;

    growing_buffer_reader_update(&replace->reader);

    pool_ref(replace->output.pool);

    replace_try_read_from_buffer(replace);
    if (replace->input == NULL)
        /* the istream API mandates that we must return 0 if the
           stream is finished */
        length = 0;

    pool_unref(replace->output.pool);

    return length;
}

static void
replace_input_eof(void *ctx)
{
    struct istream_replace *replace = ctx;

    replace->input = NULL;

    if (replace->finished)
        replace_read_check_empty(replace);
}

static void
replace_input_abort(GError *error, void *ctx)
{
    struct istream_replace *replace = ctx;

    replace_destroy(replace);
    replace->input = NULL;
    istream_deinit_abort(&replace->output, error);
}

static const struct istream_handler replace_input_handler = {
    .data = replace_input_data,
    .eof = replace_input_eof,
    .abort = replace_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_replace *
istream_to_replace(struct istream *istream)
{
    return (struct istream_replace *)(((char*)istream) - offsetof(struct istream_replace, output));
}

static off_t
istream_replace_available(struct istream *istream, bool partial)
{
    struct istream_replace *replace = istream_to_replace(istream);
    const struct substitution *subst;
    off_t length, position = 0, l;

    if (!partial && !replace->finished)
        /* we don't know yet how many substitutions will come, so we
           cannot calculate the exact rest */
        return (off_t)-1;

    /* get available bytes from replace->input */

    if (replace->input != NULL && replace->finished) {
        length = istream_available(replace->input, partial);
        if (length == (off_t)-1) {
            if (!partial)
                return (off_t)-1;
            length = 0;
        }
    } else
        length = 0;

    /* add available bytes from substitutions (and the source buffers
       before the substitutions) */

    position = replace->position;

    for (subst = replace->first_substitution; subst != NULL; subst = subst->next) {
        assert(position <= subst->start);

        length += subst->start - position;

        if (subst->istream != NULL) {
            l = istream_available(subst->istream, partial);
            if (l != (off_t)-1)
                length += l;
            else if (!partial)
                return (off_t)-1;
        }

        position = subst->end;
    }

    /* add available bytes from tail (if known yet) */

    if (replace->finished)
        length += replace->source_length - position;

    return length;
}

static void
istream_replace_read(struct istream *istream)
{
    struct istream_replace *replace = istream_to_replace(istream);

    pool_ref(replace->output.pool);

    replace_read(replace);

    if (replace->input == NULL) {
        pool_unref(replace->output.pool);
        return;
    }

    replace->had_output = false;

    do {
        replace->had_input = false;
        istream_read(replace->input);
    } while (replace->had_input && !replace->had_output &&
             replace->input != NULL);

    pool_unref(replace->output.pool);
}

static void
istream_replace_close(struct istream *istream)
{
    struct istream_replace *replace = istream_to_replace(istream);

    replace_destroy(replace);

    if (replace->input != NULL)
        istream_free_handler(&replace->input);

    istream_deinit(&replace->output);
}

static const struct istream_class istream_replace = {
    .available = istream_replace_available,
    .read = istream_replace_read,
    .close = istream_replace_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_replace_new(struct pool *pool, struct istream *input)
{
    struct istream_replace *replace = istream_new_macro(pool, replace);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&replace->input, input,
                           &replace_input_handler, replace,
                           0);

    replace->finished = false;
    replace->read_locked = false;

    replace->buffer = growing_buffer_new(replace->output.pool, 4096);
    replace->source_length = 0;
    replace->position = 0;
    replace->settled_position = 0;

    growing_buffer_reader_init(&replace->reader, replace->buffer);

    replace->first_substitution = NULL;
    replace->append_substitution_p = &replace->first_substitution;

#ifndef NDEBUG
    replace->last_substitution_end = 0;
#endif

    return istream_struct_cast(&replace->output);
}

void
istream_replace_add(struct istream *istream, off_t start, off_t end,
                    struct istream *contents)
{
    struct istream_replace *replace = istream_to_replace(istream);
    struct substitution *s;

    assert(!replace->finished);
    assert(start >= 0);
    assert(start <= end);
    assert(start >= replace->settled_position);
    assert(start >= replace->last_substitution_end);

    if (contents == NULL && start == end)
        return;

    s = p_malloc(replace->output.pool, sizeof(*s));
    s->next = NULL;
    s->replace = replace;

    s->start = start;
    s->end = end;

    replace->settled_position = end;

#ifndef NDEBUG
    replace->last_substitution_end = end;
#endif

    if (contents == NULL) {
        s->istream = NULL;
    } else {
        istream_assign_handler(&s->istream, contents,
                               &replace_substitution_handler, s,
                               0);
    }

    *replace->append_substitution_p = s;
    replace->append_substitution_p = &s->next;
}

static struct substitution *
replace_get_last_substitution(struct istream_replace *replace)
{
    struct substitution *substitution = replace->first_substitution;
    assert(substitution != NULL);

    while (substitution->next != NULL)
        substitution = substitution->next;

    assert(substitution->end <= replace->settled_position);
    assert(substitution->end == replace->last_substitution_end);
    return substitution;
}

void
istream_replace_extend(struct istream *istream, G_GNUC_UNUSED off_t start, off_t end)
{
    assert(istream != NULL);

    struct istream_replace *replace = istream_to_replace(istream);
    assert(!replace->finished);

    struct substitution *substitution = replace_get_last_substitution(replace);
    assert(substitution->start == start);
    assert(substitution->end == replace->settled_position);
    assert(substitution->end == replace->last_substitution_end);
    assert(end >= substitution->end);

    substitution->end = end;
    replace->settled_position = end;
#ifndef NDEBUG
    replace->last_substitution_end = end;
#endif
}

void
istream_replace_settle(struct istream *istream, off_t offset)
{
    struct istream_replace *replace = istream_to_replace(istream);

    assert(!replace->finished);
    assert(offset >= replace->settled_position);

    replace->settled_position = offset;
}

void
istream_replace_finish(struct istream *istream)
{
    struct istream_replace *replace = istream_to_replace(istream);

    assert(!replace->finished);

    replace->finished = true;

    if (replace->input == NULL)
        replace_read_check_empty(replace);
}
