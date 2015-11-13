/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_replace.hxx"
#include "FacadeIstream.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"
#include "pool.hxx"

#include <inline/poison.h>
#include <daemon/log.h>

#include <glib.h>

#include <assert.h>

struct ReplaceIstream final : FacadeIstream {
    struct Substitution final : IstreamHandler {
        Substitution *next = nullptr;
        ReplaceIstream &replace;
        const off_t start;
        off_t end;
        IstreamPointer istream;

        Substitution(ReplaceIstream &_replace, off_t _start, off_t _end,
                     Istream *_stream)
            :replace(_replace),
             start(_start), end(_end),
             istream(_stream, *this)
        {
        }

        gcc_pure
        bool IsActive() const;

        /* virtual methods from class IstreamHandler */
        size_t OnData(const void *data, size_t length) override;
        void OnEof() override;
        void OnError(GError *error) override;
    };

    bool finished = false, read_locked = false;
    bool had_input, had_output;

    GrowingBuffer *const buffer;
    off_t source_length = 0, position = 0;

    /**
     * The offset given by istream_replace_settle() or the end offset
     * of the last substitution (whichever is bigger).
     */
    off_t settled_position = 0;

    GrowingBufferReader reader;

    Substitution *first_substitution = nullptr,
        **append_substitution_p = &first_substitution;

#ifndef NDEBUG
    off_t last_substitution_end = 0;
#endif

    ReplaceIstream(struct pool &p, Istream &_input);

    using FacadeIstream::GetPool;
    using FacadeIstream::HasInput;

    void DestroyReplace();

    /**
     * Is the buffer at the end-of-file position?
     */
    bool IsBufferAtEOF() const {
        return position == source_length;
    }

    /**
     * Is the object at end-of-file?
     */
    bool IsEOF() const {
        return !input.IsDefined() && finished &&
            first_substitution == nullptr &&
            IsBufferAtEOF();
    }

    /**
     * Copy the next chunk from the source buffer to the istream
     * handler.
     *
     * @return 0 if the istream handler is not blocking; the number of
     * bytes remaining in the buffer if it is blocking
     */
    size_t TryReadFromBuffer();

    /**
     * Copy data from the source buffer to the istream handler.
     *
     * @return 0 if the istream handler is not blocking; the number of
     * bytes remaining in the buffer if it is blocking
     */
    size_t ReadFromBuffer(size_t max_length);

    size_t ReadFromBufferLoop(off_t end);

    void TryRead();

    void ReadCheckEmpty();

    /**
     * Read data from substitution objects.
     */
    bool ReadSubstitution();

    /**
     * Activate the next substitution object after s.
     */
    void ToNextSubstitution(ReplaceIstream::Substitution *s);

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    void _Read() override;
    void _Close() override;
};

static GQuark
replace_quark(void)
{
    return g_quark_from_static_string("replace");
}

/**
 * Is this substitution object is active, i.e. its data is the next
 * being written?
 */
bool
ReplaceIstream::Substitution::IsActive() const
{
    assert(replace.first_substitution != nullptr);
    assert(replace.first_substitution->start <= start);
    assert(start >= replace.position);

    return this == replace.first_substitution && replace.position == start;
}

void
ReplaceIstream::ToNextSubstitution(ReplaceIstream::Substitution *s)
{
    assert(first_substitution == s);
    assert(s->IsActive());
    assert(!s->istream.IsDefined());
    assert(s->start <= s->end);

    reader.Skip(s->end - s->start);
    position = s->end;

    first_substitution = s->next;
    if (first_substitution == nullptr) {
        assert(append_substitution_p == &s->next);
        append_substitution_p = &first_substitution;
    }

    p_free(&GetPool(), s);

    assert(first_substitution == nullptr ||
           first_substitution->start >= position);

    if (IsEOF()) {
        DestroyEof();
        return;
    }

    /* don't recurse if we're being called from ReadSubstitution() */
    if (!read_locked) {
        const ScopePoolRef ref(GetPool() TRACE_ARGS);
        TryRead();
    }
}

/*
 * istream handler
 *
 */

inline size_t
ReplaceIstream::Substitution::OnData(const void *data, size_t length)
{
    if (IsActive()) {
        replace.had_output = true;
        return replace.InvokeData(data, length);
    } else
        return 0;
}

inline void
ReplaceIstream::Substitution::OnEof()
{
    istream.Clear();

    if (IsActive())
        replace.ToNextSubstitution(this);
}

inline void
ReplaceIstream::Substitution::OnError(GError *error)
{
    istream.Clear();

    replace.DestroyReplace();

    if (replace.HasInput())
        replace.ClearAndCloseInput();

    replace.DestroyError(error);
}

/*
 * misc methods
 *
 */

void
ReplaceIstream::DestroyReplace()
{
    assert(source_length != (off_t)-1);

    /* source_length -1 is the "destroyed" marker */
    source_length = (off_t)-1;

    while (first_substitution != nullptr) {
        auto *s = first_substitution;
        first_substitution = s->next;

        if (s->istream.IsDefined())
            s->istream.ClearAndClose();
    }
}


bool
ReplaceIstream::ReadSubstitution()
{
    while (first_substitution != nullptr && first_substitution->IsActive()) {
        auto *s = first_substitution;

        read_locked = true;

        if (s->istream.IsDefined())
            s->istream.Read();
        else
            ToNextSubstitution(s);

        read_locked = false;

        /* we assume the substitution object is blocking if it hasn't
           reached EOF with this one call */
        if (s == first_substitution)
            return true;
    }

    return false;
}

inline size_t
ReplaceIstream::ReadFromBuffer(size_t max_length)
{
    assert(max_length > 0);

    auto src = reader.Read();
    assert(!src.IsNull());
    assert(!src.IsEmpty());

    if (src.size > max_length)
        src.size = max_length;

    had_output = true;
    size_t nbytes = InvokeData(src.data, src.size);
    assert(nbytes <= src.size);

    if (nbytes == 0)
        /* istream_replace has been closed */
        return src.size;

    reader.Consume(nbytes);
    position += nbytes;

    assert(position <= source_length);

    return src.size - nbytes;
}

inline size_t
ReplaceIstream::ReadFromBufferLoop(off_t end)
{
    assert(end > position);
    assert(end <= source_length);

    /* this loop is required to cross the GrowingBuffer borders */
    size_t rest;
    do {
#ifndef NDEBUG
        PoolNotify notify(GetPool());
#endif

        size_t max_length = (size_t)(end - position);
        rest = ReadFromBuffer(max_length);

#ifndef NDEBUG
        if (notify.Denotify()) {
            assert(rest > 0);
            break;
        }
#endif

        assert(position <= end);
    } while (rest == 0 && position < end);

    return rest;
}

size_t
ReplaceIstream::TryReadFromBuffer()
{
    off_t end;
    if (first_substitution == nullptr) {
        if (finished)
            end = source_length;
        else if (position < settled_position)
            end = settled_position;
        else
            /* block after the last substitution, unless the caller
               has already set the "finished" flag */
            return 1;

        assert(position < source_length);
    } else {
        end = first_substitution->start;
        assert(end >= position);

        if (end == position)
            return 0;
    }

    size_t rest = ReadFromBufferLoop(end);
    if (rest == 0 && position == source_length &&
        first_substitution == nullptr &&
        !input.IsDefined())
        DestroyEof();

    return rest;
}

void
ReplaceIstream::TryRead()
{
    assert(position <= source_length);

    /* read until someone (input or output) blocks */
    size_t rest;
    do {
        bool blocking = ReadSubstitution();
        if (blocking || IsBufferAtEOF() || source_length == (off_t)-1)
            break;

        rest = TryReadFromBuffer();
    } while (rest == 0 && first_substitution != nullptr);
}

void
ReplaceIstream::ReadCheckEmpty()
{
    assert(finished);
    assert(!input.IsDefined());

    if (IsEOF())
        DestroyEof();
    else {
        const ScopePoolRef ref(GetPool() TRACE_ARGS);
        TryRead();
    }
}


/*
 * input handler
 *
 */

inline size_t
ReplaceIstream::OnData(const void *data, size_t length)
{
    had_input = true;

    if (source_length >= 8 * 1024 * 1024) {
        ClearAndCloseInput();
        DestroyReplace();

        GError *error =
            g_error_new_literal(replace_quark(), 0,
                                "file too large for processor");
        DestroyError(error);
        return 0;
    }

    growing_buffer_write_buffer(buffer, data, length);
    source_length += (off_t)length;

    reader.Update();

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    TryReadFromBuffer();
    if (!input.IsDefined())
        /* the istream API mandates that we must return 0 if the
           stream is finished */
        length = 0;

    return length;
}

inline void
ReplaceIstream::OnEof()
{
    input.Clear();

    if (finished)
        ReadCheckEmpty();
}

inline void
ReplaceIstream::OnError(GError *error)
{
    DestroyReplace();
    input.Clear();
    DestroyError(error);
}

/*
 * istream implementation
 *
 */

off_t
ReplaceIstream::_GetAvailable(bool partial)
{
    off_t length, position2 = 0, l;

    if (!partial && !finished)
        /* we don't know yet how many substitutions will come, so we
           cannot calculate the exact rest */
        return (off_t)-1;

    /* get available bytes from input */

    if (HasInput() && finished) {
        length = input.GetAvailable(partial);
        if (length == (off_t)-1) {
            if (!partial)
                return (off_t)-1;
            length = 0;
        }
    } else
        length = 0;

    /* add available bytes from substitutions (and the source buffers
       before the substitutions) */

    position2 = position;

    for (auto subst = first_substitution;
         subst != nullptr; subst = subst->next) {
        assert(position2 <= subst->start);

        length += subst->start - position2;

        if (subst->istream.IsDefined()) {
            l = subst->istream.GetAvailable(partial);
            if (l != (off_t)-1)
                length += l;
            else if (!partial)
                return (off_t)-1;
        }

        position2 = subst->end;
    }

    /* add available bytes from tail (if known yet) */

    if (finished)
        length += source_length - position2;

    return length;
}

void
ReplaceIstream::_Read()
{
    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    TryRead();

    if (!HasInput())
        return;

    had_output = false;

    do {
        had_input = false;
        input.Read();
    } while (had_input && !had_output && HasInput());
}

void
ReplaceIstream::_Close()
{
    DestroyReplace();

    if (HasInput())
        ClearAndCloseInput();

    Destroy();
}

/*
 * constructor
 *
 */

inline ReplaceIstream::ReplaceIstream(struct pool &p, Istream &_input)
    :FacadeIstream(p, _input),
     buffer(growing_buffer_new(&p, 4096)),
     reader(*buffer)
{
}

Istream *
istream_replace_new(struct pool &pool, Istream &input)
{
    return NewIstream<ReplaceIstream>(pool, input);
}

void
istream_replace_add(Istream &istream, off_t start, off_t end,
                    Istream *contents)
{
    auto &replace = (ReplaceIstream &)istream;

    assert(!replace.finished);
    assert(start >= 0);
    assert(start <= end);
    assert(start >= replace.settled_position);
    assert(start >= replace.last_substitution_end);

    if (contents == nullptr && start == end)
        return;

    auto s =
        NewFromPool<ReplaceIstream::Substitution>(replace.GetPool(),
                                                  replace, start, end,
                                                  contents);

    replace.settled_position = end;

#ifndef NDEBUG
    replace.last_substitution_end = end;
#endif

    *replace.append_substitution_p = s;
    replace.append_substitution_p = &s->next;
}

static ReplaceIstream::Substitution *
replace_get_last_substitution(ReplaceIstream &replace)
{
    auto *substitution = replace.first_substitution;
    assert(substitution != nullptr);

    while (substitution->next != nullptr)
        substitution = substitution->next;

    assert(substitution->end <= replace.settled_position);
    assert(substitution->end == replace.last_substitution_end);
    return substitution;
}

void
istream_replace_extend(Istream &istream, gcc_unused off_t start, off_t end)
{
    auto &replace = (ReplaceIstream &)istream;
    assert(!replace.finished);

    auto *substitution = replace_get_last_substitution(replace);
    assert(substitution->start == start);
    assert(substitution->end == replace.settled_position);
    assert(substitution->end == replace.last_substitution_end);
    assert(end >= substitution->end);

    substitution->end = end;
    replace.settled_position = end;
#ifndef NDEBUG
    replace.last_substitution_end = end;
#endif
}

void
istream_replace_settle(Istream &istream, off_t offset)
{
    auto &replace = (ReplaceIstream &)istream;
    assert(!replace.finished);
    assert(offset >= replace.settled_position);

    replace.settled_position = offset;
}

void
istream_replace_finish(Istream &istream)
{
    auto &replace = (ReplaceIstream &)istream;
    assert(!replace.finished);

    replace.finished = true;

    if (!replace.HasInput())
        replace.ReadCheckEmpty();
}
