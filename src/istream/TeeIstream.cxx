/*
 * Copyright 2007-2019 Content Management AG
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

#include "TeeIstream.hxx"
#include "UnusedPtr.hxx"
#include "Pointer.hxx"
#include "Bucket.hxx"
#include "Handler.hxx"
#include "pool/pool.hxx"
#include "event/DeferEvent.hxx"
#include "util/Cast.hxx"

#include <assert.h>

struct TeeIstream final : IstreamHandler {

    struct Output : Istream {
        /**
         * A weak output is one which is closed automatically when all
         * "strong" outputs have been closed - it will not keep up the
         * istream_tee object alone.
         */
        bool weak;

        bool enabled = true;

        Output(struct pool &p, bool _weak) noexcept:Istream(p), weak(_weak) {}

        Output(const Output &) = delete;
        Output &operator=(const Output &) = delete;

        friend struct TeeIstream;
    };

    struct FirstOutput : Output {
        /**
         * The number of bytes provided by _FillBucketList().  This is
         * a kludge that is explained in _ConsumeBucketList().
         */
        size_t bucket_list_size;

        explicit FirstOutput(struct pool &p, bool _weak) noexcept
            :Output(p, _weak) {}

        TeeIstream &GetParent() noexcept {
            return ContainerCast(*this, &TeeIstream::first_output);
        }

        off_t _GetAvailable(bool partial) noexcept override {
            assert(enabled);

            TeeIstream &tee = GetParent();

            auto available = tee.input.GetAvailable(partial);
            if (available >= 0) {
                assert(available >= (off_t)tee.skip);
                available -= tee.skip;
            }

            return available;
        }

        //off_t Skip(off_t length) noexcept override;

        void _Read() noexcept override {
            TeeIstream &tee = GetParent();

            assert(enabled);

            tee.ReadInput();
        }

        void _FillBucketList(IstreamBucketList &list) override {
            TeeIstream &tee = GetParent();

            if (tee.skip > 0) {
                /* TODO: this can be optimized by skipping data from
                   new buckets */
                list.SetMore();
                bucket_list_size = 0;
                return;
            }

            IstreamBucketList sub;

            try {
                tee.input.FillBucketList(sub);
            } catch (...) {
                tee.defer_event.Cancel();
                enabled = false;
                tee.PostponeErrorCopyForSecond(std::current_exception());
                Destroy();
                throw;
            }

            bucket_list_size = list.SpliceBuffersFrom(sub);
        }

        size_t _ConsumeBucketList(size_t nbytes) noexcept override {
            TeeIstream &tee = GetParent();

            assert(tee.skip == 0);

            /* we must not call tee.input.ConsumeBucketList() because
               that would discard data which must still be sent to the
               second output; instead of doing that, we still remember
               how much data our input pushed to the list, and we
               consume this portion of "bytes" */

            size_t consumed = std::min(nbytes, bucket_list_size);
            Consumed(consumed);
            tee.skip += consumed;
            return consumed;
        }

        void _Close() noexcept override;
    };

    struct SecondOutput : Output {
        /**
         * Postponed by PostponeErrorCopyForSecond().
         */
        std::exception_ptr postponed_error;

        explicit SecondOutput(struct pool &p, bool _weak) noexcept
            :Output(p, _weak) {}

        ~SecondOutput() noexcept override {
            if (postponed_error)
                GetParent().defer_event.Cancel();
        }

        TeeIstream &GetParent() noexcept {
            return ContainerCast(*this, &TeeIstream::second_output);
        }

        off_t _GetAvailable(bool partial) noexcept override {
            assert(enabled);

            return GetParent().input.GetAvailable(partial);
        }

        //off_t Skip(off_t length) noexcept override;

        void _Read() noexcept override {
            TeeIstream &tee = GetParent();

            assert(enabled);

            tee.ReadInput();
        }

        void _Close() noexcept override;
    };

    FirstOutput first_output;
    SecondOutput second_output;

    IstreamPointer input;

    /**
     * This event is used to defer an input.Read() call.
     */
    DeferEvent defer_event;

    /**
     * The number of bytes to skip for output 0.  The first output has
     * already consumed this many bytes, but the second output
     * blocked.
     */
    size_t skip = 0;

    TeeIstream(struct pool &p, UnusedIstreamPtr _input, EventLoop &event_loop,
               bool first_weak, bool second_weak, bool defer_read) noexcept
        :first_output(p, first_weak),
         second_output(p, second_weak),
         input(std::move(_input), *this),
         defer_event(event_loop, BIND_THIS_METHOD(ReadInput))
    {
        if (defer_read)
            DeferRead();
    }

    static TeeIstream &CastFromFirst(Istream &first) noexcept {
        return ContainerCast((FirstOutput &)first, &TeeIstream::first_output);
    }

    struct pool &GetPool() noexcept {
        return first_output.GetPool();
    }

    void ReadInput() noexcept {
        if (second_output.enabled &&
            second_output.postponed_error != nullptr) {
            assert(!input.IsDefined());
            assert(!first_output.enabled);

            defer_event.Cancel();

            second_output.DestroyError(std::exchange(second_output.postponed_error,
                                                     std::exception_ptr()));
            return;
        }

        input.Read();
    }

    size_t Feed0(const char *data, size_t length) noexcept;
    size_t Feed1(const void *data, size_t length) noexcept;
    size_t Feed(const void *data, size_t length) noexcept;

    void DeferRead() noexcept {
        assert(input.IsDefined() || second_output.postponed_error);

        defer_event.Schedule();
    }

    void PostponeErrorCopyForSecond(std::exception_ptr e) noexcept {
        assert(!first_output.enabled);

        if (!second_output.enabled)
            return;

        assert(!second_output.postponed_error);
        second_output.postponed_error = e;
        DeferRead();
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

inline size_t
TeeIstream::Feed0(const char *data, size_t length) noexcept
{
    if (!first_output.enabled)
        return length;

    if (length <= skip)
        /* all of this has already been sent to the first input, but
           the second one didn't accept it yet */
        return length;

    /* skip the part which was already sent */
    data += skip;
    length -= skip;

    size_t nbytes = first_output.InvokeData(data, length);
    if (nbytes > 0) {
        skip += nbytes;
        return skip;
    }

    if (first_output.enabled || !second_output.enabled)
        /* first output is blocking, or both closed: give up */
        return 0;

    /* the first output has been closed inside the data() callback,
       but the second is still alive: continue with the second
       output */
    return length;
}

inline size_t
TeeIstream::Feed1(const void *data, size_t length) noexcept
{
    if (!second_output.enabled)
        return length;

    size_t nbytes = second_output.InvokeData(data, length);
    if (nbytes == 0 && !second_output.enabled &&
        first_output.enabled)
        /* during the data callback, second_output has been closed,
           but first_output continues; instead of returning 0 here,
           use first_output's result */
        return length;

    return nbytes;
}

inline size_t
TeeIstream::Feed(const void *data, size_t length) noexcept
{
    size_t nbytes0 = Feed0((const char *)data, length);
    if (nbytes0 == 0)
        return 0;

    size_t nbytes1 = Feed1(data, nbytes0);
    if (nbytes1 > 0 && first_output.enabled) {
        assert(nbytes1 <= skip);
        skip -= nbytes1;
    }

    return nbytes1;
}


/*
 * istream handler
 *
 */

size_t
TeeIstream::OnData(const void *data, size_t length) noexcept
{
    assert(input.IsDefined());

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return Feed(data, length);
}

void
TeeIstream::OnEof() noexcept
{
    assert(input.IsDefined());
    input.Clear();
    defer_event.Cancel();

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    /* clean up in reverse order */

    if (second_output.enabled) {
        second_output.enabled = false;
        second_output.DestroyEof();
    }

    if (first_output.enabled) {
        first_output.enabled = false;
        first_output.DestroyEof();
    }
}

void
TeeIstream::OnError(std::exception_ptr ep) noexcept
{
    assert(input.IsDefined());
    input.Clear();
    defer_event.Cancel();

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    /* clean up in reverse order */

    if (second_output.enabled) {
        second_output.enabled = false;
        second_output.DestroyError(ep);
    }

    if (first_output.enabled) {
        first_output.enabled = false;
        first_output.DestroyError(ep);
    }
}

/*
 * istream implementation 0
 *
 */

void
TeeIstream::FirstOutput::_Close() noexcept
{
    TeeIstream &tee = GetParent();

    assert(enabled);

    enabled = false;

    if (tee.input.IsDefined()) {
        if (!tee.second_output.enabled) {
            tee.input.ClearAndClose();
            tee.defer_event.Cancel();
        } else if (tee.second_output.weak) {
            const ScopePoolRef ref(GetPool() TRACE_ARGS);

            tee.input.ClearAndClose();
            tee.defer_event.Cancel();

            if (tee.second_output.enabled) {
                tee.second_output.enabled = false;

                tee.second_output.DestroyError(std::make_exception_ptr(std::runtime_error("closing the weak second output")));
            }
        }
    }

    if (tee.input.IsDefined() && tee.second_output.enabled &&
        tee.second_output.HasHandler())
        tee.DeferRead();

    Destroy();
}

/*
 * istream implementation 2
 *
 */

void
TeeIstream::SecondOutput::_Close() noexcept
{
    TeeIstream &tee = GetParent();

    assert(enabled);

    enabled = false;

    if (tee.input.IsDefined()) {
        if (!tee.first_output.enabled) {
            tee.input.ClearAndClose();
            tee.defer_event.Cancel();
        } else if (tee.first_output.weak) {
            const ScopePoolRef ref(tee.GetPool() TRACE_ARGS);

            tee.input.ClearAndClose();
            tee.defer_event.Cancel();

            if (tee.first_output.enabled) {
                tee.first_output.enabled = false;

                tee.first_output.DestroyError(std::make_exception_ptr(std::runtime_error("closing the weak first output")));
            }
        }
    }

    if (tee.input.IsDefined() && tee.first_output.enabled &&
        tee.first_output.HasHandler())
        tee.DeferRead();

    Destroy();
}

/*
 * constructor
 *
 */

std::pair<UnusedIstreamPtr, UnusedIstreamPtr>
istream_tee_new(struct pool &pool, UnusedIstreamPtr input,
                EventLoop &event_loop,
                bool first_weak, bool second_weak,
                bool defer_read)
{
    auto tee = NewFromPool<TeeIstream>(pool, pool, std::move(input),
                                       event_loop,
                                       first_weak, second_weak,
                                       defer_read);
    return std::make_pair(UnusedIstreamPtr(&tee->first_output),
                          UnusedIstreamPtr(&tee->second_output));
}
