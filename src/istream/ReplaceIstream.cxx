/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ReplaceIstream.hxx"
#include "FacadeIstream.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "GrowingBuffer.hxx"
#include "event/DeferEvent.hxx"
#include "util/ConstBuffer.hxx"
#include "pool/pool.hxx"
#include "pool/Notify.hxx"

#include <stdexcept>

#include <assert.h>

class ReplaceIstream final : public FacadeIstream {
    struct Substitution final : IstreamSink {
        Substitution *next = nullptr;
        ReplaceIstream &replace;
        const off_t start;
        off_t end;

        Substitution(ReplaceIstream &_replace, off_t _start, off_t _end,
                     UnusedIstreamPtr _input) noexcept
            :IstreamSink(std::move(_input)),
             replace(_replace),
             start(_start), end(_end)
        {
        }

        void Destroy() noexcept {
            this->~Substitution();
        }

        bool IsDefined() const noexcept {
            return input.IsDefined();
        }

        off_t GetAvailable(bool partial) const noexcept {
            return input.GetAvailable(partial);
        }

        void Read() noexcept {
            input.Read();
        }

        using IstreamSink::ClearAndCloseInput;

        gcc_pure
        bool IsActive() const noexcept;

        /* virtual methods from class IstreamHandler */
        size_t OnData(const void *data, size_t length) noexcept override;
        void OnEof() noexcept override;
        void OnError(std::exception_ptr ep) noexcept override;
    };

    /**
     * This event is scheduled when a #ReplaceIstreamControl method
     * call allows us to submit more data to the #IstreamHandler.
     * This avoids stalling the transfer when the last Read() call did
     * not return any data.
     */
    DeferEvent defer_read;

    bool finished = false, read_locked = false;
    bool had_input, had_output;

    GrowingBuffer buffer;
    off_t source_length = 0, position = 0;

    /**
     * The offset given by istream_replace_settle() or the end offset
     * of the last substitution (whichever is bigger).
     */
    off_t settled_position = 0;

    Substitution *first_substitution = nullptr,
        **append_substitution_p = &first_substitution;

#ifndef NDEBUG
    off_t last_substitution_end = 0;
#endif

    const SharedPoolPtr<ReplaceIstreamControl> control;

public:
    ReplaceIstream(struct pool &p, EventLoop &event_loop,
                   UnusedIstreamPtr _input) noexcept
        :FacadeIstream(p, std::move(_input)),
         defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)),
         control(SharedPoolPtr<ReplaceIstreamControl>::Make(p, *this))
    {
    }

    ~ReplaceIstream() noexcept {
        assert(control);
        assert(control->replace == this);

        control->replace = nullptr;

        defer_read.Cancel();
    }

    auto GetControl() noexcept {
        return control;
    }

    void Add(off_t start, off_t end, UnusedIstreamPtr contents) noexcept;
    void Extend(off_t start, off_t end) noexcept;
    void Settle(off_t offset) noexcept;
    void Finish() noexcept;

private:
    using FacadeIstream::GetPool;
    using FacadeIstream::HasInput;

    void DestroyReplace() noexcept;

    /**
     * Is the buffer at the end-of-file position?
     */
    bool IsBufferAtEOF() const noexcept {
        return position == source_length;
    }

    /**
     * Is the object at end-of-file?
     */
    bool IsEOF() const noexcept {
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
    size_t TryReadFromBuffer() noexcept;

    void DeferredRead() noexcept {
        TryReadFromBuffer();
    }

    /**
     * Copy data from the source buffer to the istream handler.
     *
     * @return 0 if the istream handler is not blocking; the number of
     * bytes remaining in the buffer if it is blocking
     */
    size_t ReadFromBuffer(size_t max_length) noexcept;

    size_t ReadFromBufferLoop(off_t end) noexcept;

    void TryRead() noexcept;

    void ReadCheckEmpty() noexcept;

    /**
     * Read data from substitution objects.
     */
    bool ReadSubstitution() noexcept;

    /**
     * Activate the next substitution object after s.
     */
    void ToNextSubstitution(ReplaceIstream::Substitution *s) noexcept;

    Substitution *GetLastSubstitution() noexcept;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;

public:
    /* virtual methods from class Istream */
    off_t _GetAvailable(bool partial) noexcept override;
    void _Read() noexcept override;
    void _Close() noexcept override;
};

/**
 * Is this substitution object is active, i.e. its data is the next
 * being written?
 */
bool
ReplaceIstream::Substitution::IsActive() const noexcept
{
    assert(replace.first_substitution != nullptr);
    assert(replace.first_substitution->start <= start);
    assert(start >= replace.position);

    return this == replace.first_substitution && replace.position == start;
}

void
ReplaceIstream::ToNextSubstitution(ReplaceIstream::Substitution *s) noexcept
{
    assert(first_substitution == s);
    assert(s->IsActive());
    assert(!s->IsDefined());
    assert(s->start <= s->end);

    buffer.Skip(s->end - s->start);
    position = s->end;

    first_substitution = s->next;
    if (first_substitution == nullptr) {
        assert(append_substitution_p == &s->next);
        append_substitution_p = &first_substitution;
    }

    s->Destroy();

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

size_t
ReplaceIstream::Substitution::OnData(const void *data, size_t length) noexcept
{
    if (IsActive()) {
        replace.had_output = true;
        return replace.InvokeData(data, length);
    } else
        return 0;
}

void
ReplaceIstream::Substitution::OnEof() noexcept
{
    input.Clear();

    if (IsActive())
        replace.ToNextSubstitution(this);
}

void
ReplaceIstream::Substitution::OnError(std::exception_ptr ep) noexcept
{
    ClearInput();

    replace.DestroyReplace();

    if (replace.HasInput())
        replace.ClearAndCloseInput();

    replace.DestroyError(ep);
}

/*
 * misc methods
 *
 */

void
ReplaceIstream::DestroyReplace() noexcept
{
    assert(source_length != (off_t)-1);

    /* source_length -1 is the "destroyed" marker */
    source_length = (off_t)-1;

    while (first_substitution != nullptr) {
        auto *s = first_substitution;
        first_substitution = s->next;

        if (s->IsDefined())
            s->ClearAndCloseInput();
    }
}


bool
ReplaceIstream::ReadSubstitution() noexcept
{
    while (first_substitution != nullptr && first_substitution->IsActive()) {
        auto *s = first_substitution;

        read_locked = true;

        if (s->IsDefined())
            s->Read();
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
ReplaceIstream::ReadFromBuffer(size_t max_length) noexcept
{
    assert(max_length > 0);

    auto src = buffer.Read();
    assert(!src.IsNull());
    assert(!src.empty());

    if (src.size > max_length)
        src.size = max_length;

    had_output = true;
    size_t nbytes = InvokeData(src.data, src.size);
    assert(nbytes <= src.size);

    if (nbytes == 0)
        /* istream_replace has been closed */
        return src.size;

    buffer.Consume(nbytes);
    position += nbytes;

    assert(position <= source_length);

    return src.size - nbytes;
}

inline size_t
ReplaceIstream::ReadFromBufferLoop(off_t end) noexcept
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
        if (notify.IsDestroyed()) {
            assert(rest > 0);
            break;
        }
#endif

        assert(position <= end);
    } while (rest == 0 && position < end);

    return rest;
}

size_t
ReplaceIstream::TryReadFromBuffer() noexcept
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
ReplaceIstream::TryRead() noexcept
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
ReplaceIstream::ReadCheckEmpty() noexcept
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

size_t
ReplaceIstream::OnData(const void *data, size_t length) noexcept
{
    had_input = true;

    if (source_length >= 8 * 1024 * 1024) {
        ClearAndCloseInput();
        DestroyReplace();

        DestroyError(std::make_exception_ptr(std::runtime_error("file too large for processor")));
        return 0;
    }

    buffer.Write(data, length);
    source_length += (off_t)length;

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    TryReadFromBuffer();
    if (!input.IsDefined())
        /* the istream API mandates that we must return 0 if the
           stream is finished */
        length = 0;

    return length;
}

void
ReplaceIstream::OnEof() noexcept
{
    input.Clear();

    if (finished)
        ReadCheckEmpty();
}

void
ReplaceIstream::OnError(std::exception_ptr ep) noexcept
{
    DestroyReplace();
    input.Clear();
    DestroyError(ep);
}

/*
 * istream implementation
 *
 */

off_t
ReplaceIstream::_GetAvailable(bool partial) noexcept
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

        if (subst->IsDefined()) {
            l = subst->GetAvailable(partial);
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
ReplaceIstream::_Read() noexcept
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
ReplaceIstream::_Close() noexcept
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

std::pair<UnusedIstreamPtr, SharedPoolPtr<ReplaceIstreamControl>>
istream_replace_new(EventLoop &event_loop, struct pool &pool,
                    UnusedIstreamPtr input) noexcept
{
    auto *i = NewIstream<ReplaceIstream>(pool, event_loop, std::move(input));
    return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}

inline void
ReplaceIstream::Add(off_t start, off_t end,
                    UnusedIstreamPtr contents) noexcept
{
    assert(!finished);
    assert(start >= 0);
    assert(start <= end);
    assert(start >= settled_position);
    assert(start >= last_substitution_end);

    if (!contents && start == end)
        return;

    auto s = NewFromPool<Substitution>(GetPool(), *this, start, end,
                                       std::move(contents));

    settled_position = end;

#ifndef NDEBUG
    last_substitution_end = end;
#endif

    *append_substitution_p = s;
    append_substitution_p = &s->next;

    defer_read.Schedule();
}

void
ReplaceIstreamControl::Add(off_t start, off_t end,
                           UnusedIstreamPtr contents) noexcept
{
    if (replace != nullptr)
        replace->Add(start, end, std::move(contents));
}

inline ReplaceIstream::Substitution *
ReplaceIstream::GetLastSubstitution() noexcept
{
    auto *substitution = first_substitution;
    assert(substitution != nullptr);

    while (substitution->next != nullptr)
        substitution = substitution->next;

    assert(substitution->end <= settled_position);
    assert(substitution->end == last_substitution_end);
    return substitution;
}

inline void
ReplaceIstream::Extend(gcc_unused off_t start, off_t end) noexcept
{
    assert(!finished);

    auto *substitution = GetLastSubstitution();
    assert(substitution->start == start);
    assert(substitution->end == settled_position);
    assert(substitution->end == last_substitution_end);
    assert(end >= substitution->end);

    substitution->end = end;
    settled_position = end;
#ifndef NDEBUG
    last_substitution_end = end;
#endif
}

void
ReplaceIstreamControl::Extend(off_t start, off_t end) noexcept
{
    if (replace != nullptr)
        replace->Extend(start, end);
}

inline void
ReplaceIstream::Settle(off_t offset) noexcept
{
    assert(!finished);
    assert(offset >= settled_position);

    settled_position = offset;

    defer_read.Schedule();
}

void
ReplaceIstreamControl::Settle(off_t offset) noexcept
{
    if (replace != nullptr)
        replace->Settle(offset);
}

inline void
ReplaceIstream::Finish() noexcept
{
    assert(!finished);

    finished = true;

    if (!HasInput())
        ReadCheckEmpty();
}

void
ReplaceIstreamControl::Finish() noexcept
{
    if (replace != nullptr)
        replace->Finish();
}
