/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_replace.hxx"
#include "istream-internal.h"
#include "growing_buffer.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "pool.hxx"

#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>

struct ReplaceIstream;

struct substitution {
    struct substitution *next;
    ReplaceIstream *replace;
    off_t start, end;
    struct istream *istream;
};

struct ReplaceIstream {
    struct istream output;
    struct istream *input;

    bool finished = false, read_locked = false;
    bool had_input, had_output;

    GrowingBuffer *buffer;
    off_t source_length = 0, position = 0;

    /**
     * The offset given by istream_replace_settle() or the end offset
     * of the last substitution (whichever is bigger).
     */
    off_t settled_position = 0;

    GrowingBufferReader reader;

    struct substitution *first_substitution = nullptr,
        **append_substitution_p = &first_substitution;

#ifndef NDEBUG
    off_t last_substitution_end = 0;
#endif

    explicit ReplaceIstream(struct pool &p);
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
replace_buffer_eof(const ReplaceIstream *replace)
{
    return replace->position == replace->source_length;
}

/**
 * Is the object at end-of-file?
 */
static inline bool
replace_is_eof(const ReplaceIstream *replace)
{
    return replace->input == nullptr && replace->finished &&
        replace->first_substitution == nullptr &&
        replace_buffer_eof(replace);
}

/**
 * Is this substitution object is active, i.e. its data is the next
 * being written?
 */
static inline bool
substitution_is_active(const struct substitution *s)
{
    const ReplaceIstream *replace = s->replace;

    assert(replace != nullptr);
    assert(replace->first_substitution != nullptr);
    assert(replace->first_substitution->start <= s->start);
    assert(s->start >= replace->position);

    return s == replace->first_substitution &&
        replace->position == s->start;
}

static void
replace_read(ReplaceIstream *replace);

/**
 * Activate the next substitution object after s.
 */
static void
replace_to_next_substitution(ReplaceIstream *replace, struct substitution *s)
{
    assert(replace->first_substitution == s);
    assert(substitution_is_active(s));
    assert(s->istream == nullptr);
    assert(s->start <= s->end);

    replace->reader.Skip(s->end - s->start);
    replace->position = s->end;

    replace->first_substitution = s->next;
    if (replace->first_substitution == nullptr) {
        assert(replace->append_substitution_p == &s->next);
        replace->append_substitution_p = &replace->first_substitution;
    }

    p_free(replace->output.pool, s);

    assert(replace->first_substitution == nullptr ||
           replace->first_substitution->start >= replace->position);

    if (replace_is_eof(replace)) {
        istream_deinit_eof(&replace->output);
        return;
    }

    /* don't recurse if we're being called
       replace_read_substitution() */
    if (!replace->read_locked) {
        const ScopePoolRef ref(*replace->output.pool TRACE_ARGS);
        replace_read(replace);
    }
}

static void
replace_destroy(ReplaceIstream *replace);


/*
 * istream handler
 *
 */

static size_t
replace_substitution_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = (struct substitution *)ctx;
    ReplaceIstream *replace = s->replace;

    if (substitution_is_active(s)) {
        replace->had_output = true;
        return istream_invoke_data(&replace->output, data, length);
    } else
        return 0;
}

static void
replace_substitution_eof(void *ctx)
{
    struct substitution *s = (struct substitution *)ctx;
    ReplaceIstream *replace = s->replace;

    s->istream = nullptr;

    if (substitution_is_active(s))
        replace_to_next_substitution(replace, s);
}

static void
replace_substitution_abort(GError *error, void *ctx)
{
    struct substitution *s = (struct substitution *)ctx;
    ReplaceIstream *replace = s->replace;

    s->istream = nullptr;

    replace_destroy(replace);

    if (replace->input != nullptr)
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
replace_destroy(ReplaceIstream *replace)
{
    assert(replace != nullptr);
    assert(replace->source_length != (off_t)-1);

    /* source_length -1 is the "destroyed" marker */
    replace->source_length = (off_t)-1;

    while (replace->first_substitution != nullptr) {
        struct substitution *s = replace->first_substitution;
        replace->first_substitution = s->next;

        if (s->istream != nullptr)
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
replace_read_substitution(ReplaceIstream *replace)
{
    while (replace->first_substitution != nullptr &&
           substitution_is_active(replace->first_substitution)) {
        struct substitution *s = replace->first_substitution;

        replace->read_locked = true;

        if (s->istream == nullptr)
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
replace_read_from_buffer(ReplaceIstream *replace, size_t max_length)
{
    assert(replace != nullptr);
    assert(max_length > 0);

    auto src = replace->reader.Read();
    assert(!src.IsNull());
    assert(!src.IsEmpty());

    if (src.size > max_length)
        src.size = max_length;

    replace->had_output = true;
    size_t nbytes = istream_invoke_data(&replace->output, src.data, src.size);
    assert(nbytes <= src.size);

    if (nbytes == 0)
        /* istream_replace has been closed */
        return src.size;

    replace->reader.Consume(nbytes);
    replace->position += nbytes;

    assert(replace->position <= replace->source_length);

    return src.size - nbytes;
}

static size_t
replace_read_from_buffer_loop(ReplaceIstream *replace, off_t end)
{
    size_t max_length, rest;

    assert(replace != nullptr);
    assert(end > replace->position);
    assert(end <= replace->source_length);

    /* this loop is required to cross the growing_buffer borders */
    do {
#ifndef NDEBUG
        PoolNotify notify(*replace->output.pool);
#endif

        max_length = (size_t)(end - replace->position);
        rest = replace_read_from_buffer(replace, max_length);

#ifndef NDEBUG
        if (notify.Denotify()) {
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
replace_try_read_from_buffer(ReplaceIstream *replace)
{
    off_t end;
    size_t rest;

    assert(replace != nullptr);

    if (replace->first_substitution == nullptr) {
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
        replace->first_substitution == nullptr &&
        replace->input == nullptr)
        istream_deinit_eof(&replace->output);

    return rest;
}

static void
replace_read(ReplaceIstream *replace)
{
    bool blocking;
    size_t rest;

    assert(replace != nullptr);
    assert(replace->position <= replace->source_length);

    /* read until someone (input or output) blocks */
    do {
        blocking = replace_read_substitution(replace);
        if (blocking || replace_buffer_eof(replace) ||
            replace->source_length == (off_t)-1)
            break;

        rest = replace_try_read_from_buffer(replace);
    } while (rest == 0 && replace->first_substitution != nullptr);
}

static void
replace_read_check_empty(ReplaceIstream *replace)
{
    assert(replace != nullptr);
    assert(replace->finished);
    assert(replace->input == nullptr);

    if (replace_is_eof(replace))
        istream_deinit_eof(&replace->output);
    else {
        const ScopePoolRef ref(*replace->output.pool TRACE_ARGS);
        replace_read(replace);
    }
}


/*
 * input handler
 *
 */

static size_t
replace_input_data(const void *data, size_t length, void *ctx)
{
    ReplaceIstream *replace = (ReplaceIstream *)ctx;

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

    replace->reader.Update();

    const ScopePoolRef ref(*replace->output.pool TRACE_ARGS);

    replace_try_read_from_buffer(replace);
    if (replace->input == nullptr)
        /* the istream API mandates that we must return 0 if the
           stream is finished */
        length = 0;

    return length;
}

static void
replace_input_eof(void *ctx)
{
    ReplaceIstream *replace = (ReplaceIstream *)ctx;

    replace->input = nullptr;

    if (replace->finished)
        replace_read_check_empty(replace);
}

static void
replace_input_abort(GError *error, void *ctx)
{
    ReplaceIstream *replace = (ReplaceIstream *)ctx;

    replace_destroy(replace);
    replace->input = nullptr;
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

static inline ReplaceIstream *
istream_to_replace(struct istream *istream)
{
    return &ContainerCast2(*istream, &ReplaceIstream::output);
}

static off_t
istream_replace_available(struct istream *istream, bool partial)
{
    ReplaceIstream *replace = istream_to_replace(istream);
    const struct substitution *subst;
    off_t length, position = 0, l;

    if (!partial && !replace->finished)
        /* we don't know yet how many substitutions will come, so we
           cannot calculate the exact rest */
        return (off_t)-1;

    /* get available bytes from replace->input */

    if (replace->input != nullptr && replace->finished) {
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

    for (subst = replace->first_substitution; subst != nullptr; subst = subst->next) {
        assert(position <= subst->start);

        length += subst->start - position;

        if (subst->istream != nullptr) {
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
    ReplaceIstream *replace = istream_to_replace(istream);

    const ScopePoolRef ref(*replace->output.pool TRACE_ARGS);

    replace_read(replace);

    if (replace->input == nullptr)
        return;

    replace->had_output = false;

    do {
        replace->had_input = false;
        istream_read(replace->input);
    } while (replace->had_input && !replace->had_output &&
             replace->input != nullptr);
}

static void
istream_replace_close(struct istream *istream)
{
    ReplaceIstream *replace = istream_to_replace(istream);

    replace_destroy(replace);

    if (replace->input != nullptr)
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

inline ReplaceIstream::ReplaceIstream(struct pool &p)
    :buffer(growing_buffer_new(&p, 4096)),
     reader(*buffer)
{
    istream_init(&output, &::istream_replace, &p);
}

struct istream *
istream_replace_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto *replace = NewFromPool<ReplaceIstream>(*pool, *pool);

    istream_assign_handler(&replace->input, input,
                           &replace_input_handler, replace,
                           0);

    return istream_struct_cast(&replace->output);
}

void
istream_replace_add(struct istream *istream, off_t start, off_t end,
                    struct istream *contents)
{
    ReplaceIstream *replace = istream_to_replace(istream);

    assert(!replace->finished);
    assert(start >= 0);
    assert(start <= end);
    assert(start >= replace->settled_position);
    assert(start >= replace->last_substitution_end);

    if (contents == nullptr && start == end)
        return;

    auto s = NewFromPool<struct substitution>(*replace->output.pool);
    s->next = nullptr;
    s->replace = replace;

    s->start = start;
    s->end = end;

    replace->settled_position = end;

#ifndef NDEBUG
    replace->last_substitution_end = end;
#endif

    if (contents == nullptr) {
        s->istream = nullptr;
    } else {
        istream_assign_handler(&s->istream, contents,
                               &replace_substitution_handler, s,
                               0);
    }

    *replace->append_substitution_p = s;
    replace->append_substitution_p = &s->next;
}

static struct substitution *
replace_get_last_substitution(ReplaceIstream *replace)
{
    struct substitution *substitution = replace->first_substitution;
    assert(substitution != nullptr);

    while (substitution->next != nullptr)
        substitution = substitution->next;

    assert(substitution->end <= replace->settled_position);
    assert(substitution->end == replace->last_substitution_end);
    return substitution;
}

void
istream_replace_extend(struct istream *istream, gcc_unused off_t start, off_t end)
{
    assert(istream != nullptr);

    ReplaceIstream *replace = istream_to_replace(istream);
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
    ReplaceIstream *replace = istream_to_replace(istream);

    assert(!replace->finished);
    assert(offset >= replace->settled_position);

    replace->settled_position = offset;
}

void
istream_replace_finish(struct istream *istream)
{
    ReplaceIstream *replace = istream_to_replace(istream);

    assert(!replace->finished);

    replace->finished = true;

    if (replace->input == nullptr)
        replace_read_check_empty(replace);
}
