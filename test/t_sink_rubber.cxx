#include "TestPool.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "pool.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream_four.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
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

    Rubber *r;

    unsigned rubber_id;
    size_t size;
    std::exception_ptr error;

    CancellablePointer cancel_ptr;

    Data(Rubber *_r):result(NONE), r(_r), rubber_id(0) {}
    ~Data() {
        if (rubber_id > 0)
            rubber_remove(r, rubber_id);
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

class ScopeRubber {
    Rubber *const r;

public:
    ScopeRubber()
        :r(rubber_new(4 * 1024 * 1024)) {}

    ScopeRubber(const ScopeRubber &) = delete;

    ~ScopeRubber() {
        rubber_free(r);
    }

    operator Rubber *() {
        return r;
    }

    operator Rubber &() {
        return *r;
    }
};

TEST(SinkRubberTest, Empty)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_null_new(pool);
    sink_rubber_new(pool, *input, *r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_EQ(0u, data.rubber_id);
    ASSERT_EQ(size_t(0), data.size);
}

TEST(SinkRubberTest, Empty2)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_byte_new(pool,
                                      *istream_null_new(pool));
    sink_rubber_new(pool, *input, r, 1024,
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
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_string_new(pool, "foo");
    sink_rubber_new(pool, *input, r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);
    input->Read();

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_GT(data.rubber_id, 0);
    ASSERT_EQ(size_t(3), data.size);
    ASSERT_EQ(size_t(32), rubber_size_of(r, data.rubber_id));
    ASSERT_EQ(0, memcmp("foo",
                                   rubber_read(r, data.rubber_id), 3));
}

TEST(SinkRubberTest, String2)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_four_new(pool,
                                      *istream_string_new(pool,
                                                          "foobar"));
    sink_rubber_new(pool, *input, r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);

    input->Read();
    if (Data::NONE == data.result)
        input->Read();

    ASSERT_EQ(Data::DONE, data.result);
    ASSERT_GT(data.rubber_id, 0);
    ASSERT_EQ(size_t(6), data.size);
    ASSERT_EQ(size_t(32), rubber_size_of(r, data.rubber_id));
    ASSERT_EQ(0, memcmp("foobar",
                                   rubber_read(r, data.rubber_id), 6));
}

TEST(SinkRubberTest, TooLarge1)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_string_new(pool, "foobar");
    sink_rubber_new(pool, *input, r, 5,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::TOO_LARGE, data.result);
}

TEST(SinkRubberTest, TooLarge2)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_four_new(pool,
                                      *istream_string_new(pool,
                                                          "foobar"));
    sink_rubber_new(pool, *input, r, 5,
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
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_fail_new(pool,
                                      std::make_exception_ptr(std::runtime_error("error")));
    sink_rubber_new(pool, *input, r, 1024,
                    data, data.cancel_ptr);

    ASSERT_EQ(Data::NONE, data.result);
    input->Read();

    ASSERT_EQ(Data::ERROR, data.result);
    ASSERT_NE(data.error, nullptr);
}

TEST(SinkRubberTest, OOM)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *input = istream_delayed_new(pool);
    istream_delayed_cancellable_ptr(*input) = nullptr;

    sink_rubber_new(pool, *input, r, 8 * 1024 * 1024,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::OOM, data.result);
}

TEST(SinkRubberTest, Abort)
{
    TestPool pool;
    ScopeRubber r;
    Data data(r);

    Istream *delayed = istream_delayed_new(pool);
    istream_delayed_cancellable_ptr(*delayed) = nullptr;

    Istream *input = istream_cat_new(pool,
                                     istream_string_new(pool, "foo"),
                                     delayed);
    sink_rubber_new(pool, *input, r, 4,
                    data, data.cancel_ptr);
    ASSERT_EQ(Data::NONE, data.result);
    input->Read();
    ASSERT_EQ(Data::NONE, data.result);

    data.cancel_ptr.Cancel();
}
