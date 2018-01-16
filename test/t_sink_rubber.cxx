/*
 * Copyright 2007-2017 Content Management AG
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

#include "TestPool.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "pool/pool.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream_four.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Cancellable.hxx"

#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct Data final : RubberSinkHandler {
    enum Result {
        NONE, DONE, OOM, TOO_LARGE, ERROR
    } result;

    Rubber &r;

    unsigned rubber_id;
    size_t size;
    std::exception_ptr error;

    CancellablePointer cancel_ptr;

    explicit Data(Rubber &_r):result(NONE), r(_r), rubber_id(0) {}
    ~Data() {
        if (rubber_id > 0)
            r.Remove(rubber_id);
    }

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(std::exception_ptr ep) override;
};

void
Data::RubberDone(unsigned _rubber_id, size_t _size)
{
    assert(result == NONE);

    result = DONE;
    rubber_id = _rubber_id;
    size = _size;
}

void
Data::RubberOutOfMemory()
{
    assert(result == NONE);

    result = OOM;
}

void
Data::RubberTooLarge()
{
    assert(result == NONE);

    result = TOO_LARGE;
}

void
Data::RubberError(std::exception_ptr ep)
{
    assert(result == NONE);

    result = ERROR;
    error = ep;
}

TEST(SinkRubberTest, Empty)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    sink_rubber_new(pool, istream_null_new(pool), r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_EQ(0u, data.rubber_id);
    ASSERT_EQ(size_t(0), data.size);
}

TEST(SinkRubberTest, Empty2)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_byte_new(pool,
                                      *istream_null_new(pool).Steal());
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);
    input->Read();

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_EQ(0u, data.rubber_id);
    ASSERT_EQ(size_t(0), data.size);
}

TEST(SinkRubberTest, String)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_string_new(pool, "foo").Steal();
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);
    input->Read();

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_GT(data.rubber_id, 0);
    ASSERT_EQ(size_t(3), data.size);
    ASSERT_EQ(size_t(32), r.GetSizeOf(data.rubber_id));
    ASSERT_EQ(0, memcmp("foo", r.Read(data.rubber_id), 3));
}

TEST(SinkRubberTest, String2)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_four_new(pool,
                                      *istream_string_new(*pool,
                                                          "foobar").Steal());
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);

    input->Read();
    if (Data::NONE == data.result)
        input->Read();

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_GT(data.rubber_id, 0);
    ASSERT_EQ(size_t(6), data.size);
    ASSERT_EQ(size_t(32), r.GetSizeOf(data.rubber_id));
    ASSERT_EQ(0, memcmp("foobar", r.Read(data.rubber_id), 6));
}

TEST(SinkRubberTest, TooLarge1)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    sink_rubber_new(pool, istream_string_new(*pool, "foobar"), r, 5,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::TOO_LARGE, data.result);
}

TEST(SinkRubberTest, TooLarge2)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_four_new(pool,
                                      *istream_string_new(*pool,
                                                          "foobar").Steal());
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 5,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);

    input->Read();
    if (Data::NONE == data.result)
        input->Read();

    ASSERT_EQ(Data::TOO_LARGE, data.result);
}

TEST(SinkRubberTest, Error)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_fail_new(pool,
                                      std::make_exception_ptr(std::runtime_error("error")));
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);
    input->Read();

    ASSERT_EQ(Data::ERROR, data.result);
    ASSERT_NE(data.error, nullptr);
}

TEST(SinkRubberTest, OOM)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *input = istream_delayed_new(pool);
    istream_delayed_cancellable_ptr(*input) = nullptr;

    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 8 * 1024 * 1024,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::OOM, data.result);
}

TEST(SinkRubberTest, Abort)
{
    TestPool pool;
    Rubber r(4 * 1024 * 1024);
    Data data(r);

    Istream *delayed = istream_delayed_new(pool);
    istream_delayed_cancellable_ptr(*delayed) = nullptr;

    Istream *input = istream_cat_new(pool,
                                     istream_string_new(*pool, "foo").Steal(),
                                     delayed);
    sink_rubber_new(pool, UnusedIstreamPtr(input), r, 4,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::NONE, data.result);
    input->Read();
    ASSERT_EQ(Data::NONE, data.result);

    data.cancel_ptr.Cancel();
}
