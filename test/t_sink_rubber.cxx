#include "PoolTest.hxx"
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

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <glib.h>

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

class SinkRubberTest : public PoolTest {
    CPPUNIT_TEST_SUITE(SinkRubberTest);
    CPPUNIT_TEST(TestEmpty);
    CPPUNIT_TEST(TestString);
    CPPUNIT_TEST(TestString2);
    CPPUNIT_TEST(TestTooLarge1);
    CPPUNIT_TEST(TestTooLarge2);
    CPPUNIT_TEST(TestError);
    CPPUNIT_TEST(TestOOM);
    CPPUNIT_TEST(TestAbort);
    CPPUNIT_TEST_SUITE_END();

    Rubber *r;

public:
    virtual void setUp() {
        PoolTest::setUp();

        size_t total = 4 * 1024 * 1024;
        r = rubber_new(total);
    }

    virtual void tearDown() {
        rubber_free(r);

        PoolTest::tearDown();
    }

    void TestEmpty() {
        Data data(r);

        Istream *input = istream_null_new(GetPool());
        sink_rubber_new(*GetPool(), *input, *r, 1024,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT_EQUAL(0u, data.rubber_id);
        CPPUNIT_ASSERT_EQUAL(size_t(0), data.size);
    }

    void TestEmpty2() {
        Data data(r);

        Istream *input = istream_byte_new(*GetPool(),
                                          *istream_null_new(GetPool()));
        sink_rubber_new(*GetPool(), *input, *r, 1024,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        input->Read();

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT_EQUAL(0u, data.rubber_id);
        CPPUNIT_ASSERT_EQUAL(size_t(0), data.size);
    }

    void TestString() {
        Data data(r);

        Istream *input = istream_string_new(GetPool(), "foo");
        sink_rubber_new(*GetPool(), *input, *r, 1024,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        input->Read();

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT(data.rubber_id > 0);
        CPPUNIT_ASSERT_EQUAL(size_t(3), data.size);
        CPPUNIT_ASSERT_EQUAL(size_t(32), rubber_size_of(r, data.rubber_id));
        CPPUNIT_ASSERT_EQUAL(0, memcmp("foo",
                                       rubber_read(r, data.rubber_id), 3));
    }

    void TestString2() {
        Data data(r);

        Istream *input = istream_four_new(GetPool(),
                                          *istream_string_new(GetPool(),
                                                              "foobar"));
        sink_rubber_new(*GetPool(), *input, *r, 1024,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        input->Read();
        if (Data::NONE == data.result)
            input->Read();

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT(data.rubber_id > 0);
        CPPUNIT_ASSERT_EQUAL(size_t(6), data.size);
        CPPUNIT_ASSERT_EQUAL(size_t(32), rubber_size_of(r, data.rubber_id));
        CPPUNIT_ASSERT_EQUAL(0, memcmp("foobar",
                                       rubber_read(r, data.rubber_id), 6));
    }

    void TestTooLarge1() {
        Data data(r);

        Istream *input = istream_string_new(GetPool(), "foobar");
        sink_rubber_new(*GetPool(), *input, *r, 5,
                        data, data.cancel_ptr);
        CPPUNIT_ASSERT_EQUAL(Data::TOO_LARGE, data.result);
    }

    void TestTooLarge2() {
        Data data(r);

        Istream *input = istream_four_new(GetPool(),
                                          *istream_string_new(GetPool(),
                                                             "foobar"));
        sink_rubber_new(*GetPool(), *input, *r, 5,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        input->Read();
        if (Data::NONE == data.result)
            input->Read();

        CPPUNIT_ASSERT_EQUAL(Data::TOO_LARGE, data.result);
    }

    void TestError() {
        Data data(r);

        Istream *input = istream_fail_new(GetPool(),
                                          g_error_new(g_file_error_quark(), 0, "error"));
        sink_rubber_new(*GetPool(), *input, *r, 1024,
                        data, data.cancel_ptr);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        input->Read();

        CPPUNIT_ASSERT_EQUAL(Data::ERROR, data.result);
        CPPUNIT_ASSERT(data.error != NULL);
    }

    void TestOOM() {
        Data data(r);

        Istream *input = istream_delayed_new(GetPool());
        istream_delayed_cancellable_ptr(*input) = nullptr;

        sink_rubber_new(*GetPool(), *input, *r, 8 * 1024 * 1024,
                        data, data.cancel_ptr);
        CPPUNIT_ASSERT_EQUAL(Data::OOM, data.result);
    }

    void TestAbort() {
        Data data(r);

        Istream *delayed = istream_delayed_new(GetPool());
        istream_delayed_cancellable_ptr(*delayed) = nullptr;

        Istream *input = istream_cat_new(*GetPool(),
                                         istream_string_new(GetPool(), "foo"),
                                         delayed);
        sink_rubber_new(*GetPool(), *input, *r, 4,
                        data, data.cancel_ptr);
        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        input->Read();
        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        data.cancel_ptr.Cancel();
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SinkRubberTest);

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    CppUnit::Test *suite =
        CppUnit::TestFactoryRegistry::getRegistry().makeTest();

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(suite);

    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(),
                                                       std::cerr));
    bool success = runner.run();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
