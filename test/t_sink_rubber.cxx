#include "PoolTest.hxx"
#include "async.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "pool.hxx"
#include "istream-impl.h"
#include "istream_null.hxx"
#include "istream_byte.hxx"
#include "istream.h"

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

struct Data {
    enum Result {
        NONE, DONE, OOM, TOO_LARGE, ERROR
    } result;

    Rubber *r;

    unsigned rubber_id;
    size_t size;
    GError *error;

    struct async_operation_ref async_ref;

    Data(Rubber *_r):result(NONE), r(_r), rubber_id(0), error(NULL) {}
    ~Data() {
        if (error != NULL)
            g_error_free(error);
        if (rubber_id > 0)
            rubber_remove(r, rubber_id);
    }
};

static void
my_sink_rubber_done(unsigned rubber_id, size_t size, void *ctx)
{
    Data *data = (Data *)ctx;
    assert(data->result == Data::NONE);

    data->result = Data::DONE;
    data->rubber_id = rubber_id;
    data->size = size;
}

static void
my_sink_rubber_oom(void *ctx)
{
    Data *data = (Data *)ctx;
    assert(data->result == Data::NONE);

    data->result = Data::OOM;
}

static void
my_sink_rubber_too_large(void *ctx)
{
    Data *data = (Data *)ctx;
    assert(data->result == Data::NONE);

    data->result = Data::TOO_LARGE;
}

static void
my_sink_rubber_error(GError *error, void *ctx)
{
    Data *data = (Data *)ctx;
    assert(data->result == Data::NONE);

    data->result = Data::ERROR;
    data->error = error;
}

static const struct sink_rubber_handler my_sink_rubber_handler = {
    /*.done =*/ my_sink_rubber_done,
    /*.out_of_memory =*/ my_sink_rubber_oom,
    /*.too_large =*/ my_sink_rubber_too_large,
    /*.error =*/ my_sink_rubber_error,
};

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

        istream *input = istream_null_new(GetPool());
        sink_rubber_new(GetPool(), input, r, 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT_EQUAL(0u, data.rubber_id);
        CPPUNIT_ASSERT_EQUAL(size_t(0), data.size);
    }

    void TestEmpty2() {
        Data data(r);

        istream *input = istream_byte_new(GetPool(),
                                          istream_null_new(GetPool()));
        sink_rubber_new(GetPool(), input, r, 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        istream_read(input);

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT_EQUAL(0u, data.rubber_id);
        CPPUNIT_ASSERT_EQUAL(size_t(0), data.size);
    }

    void TestString() {
        Data data(r);

        istream *input = istream_string_new(GetPool(), "foo");
        sink_rubber_new(GetPool(), input, r, 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        istream_read(input);

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT(data.rubber_id > 0);
        CPPUNIT_ASSERT_EQUAL(size_t(3), data.size);
        CPPUNIT_ASSERT_EQUAL(size_t(32), rubber_size_of(r, data.rubber_id));
        CPPUNIT_ASSERT_EQUAL(0, memcmp("foo",
                                       rubber_read(r, data.rubber_id), 3));
    }

    void TestString2() {
        Data data(r);

        istream *input = istream_four_new(GetPool(),
                                          istream_string_new(GetPool(),
                                                             "foobar"));
        sink_rubber_new(GetPool(), input, r, 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        istream_read(input);
        if (Data::NONE == data.result)
            istream_read(input);

        CPPUNIT_ASSERT_EQUAL(Data::DONE, data.result);
        CPPUNIT_ASSERT(data.rubber_id > 0);
        CPPUNIT_ASSERT_EQUAL(size_t(6), data.size);
        CPPUNIT_ASSERT_EQUAL(size_t(32), rubber_size_of(r, data.rubber_id));
        CPPUNIT_ASSERT_EQUAL(0, memcmp("foobar",
                                       rubber_read(r, data.rubber_id), 6));
    }

    void TestTooLarge1() {
        Data data(r);

        istream *input = istream_string_new(GetPool(), "foobar");
        sink_rubber_new(GetPool(), input, r, 5,
                        &my_sink_rubber_handler, &data, &data.async_ref);
        CPPUNIT_ASSERT_EQUAL(Data::TOO_LARGE, data.result);
    }

    void TestTooLarge2() {
        Data data(r);

        istream *input = istream_four_new(GetPool(),
                                          istream_string_new(GetPool(),
                                                             "foobar"));
        sink_rubber_new(GetPool(), input, r, 5,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        istream_read(input);
        if (Data::NONE == data.result)
            istream_read(input);

        CPPUNIT_ASSERT_EQUAL(Data::TOO_LARGE, data.result);
    }

    void TestError() {
        Data data(r);

        istream *input = istream_fail_new(GetPool(),
                                          g_error_new(g_file_error_quark(), 0, "error"));
        sink_rubber_new(GetPool(), input, r, 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);

        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        istream_read(input);

        CPPUNIT_ASSERT_EQUAL(Data::ERROR, data.result);
        CPPUNIT_ASSERT(data.error != NULL);
    }

    void TestOOM() {
        Data data(r);

        istream *input = istream_delayed_new(GetPool());
        istream_delayed_async_ref(input)->Clear();

        sink_rubber_new(GetPool(), input, r, 8 * 1024 * 1024,
                        &my_sink_rubber_handler, &data, &data.async_ref);
        CPPUNIT_ASSERT_EQUAL(Data::OOM, data.result);
    }

    void TestAbort() {
        Data data(r);

        istream *delayed = istream_delayed_new(GetPool());
        istream_delayed_async_ref(delayed)->Clear();

        istream *input = istream_cat_new(GetPool(),
                                         istream_string_new(GetPool(), "foo"),
                                         delayed,
                                         NULL);
        sink_rubber_new(GetPool(), input, r, 4,
                        &my_sink_rubber_handler, &data, &data.async_ref);
        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);
        istream_read(input);
        CPPUNIT_ASSERT_EQUAL(Data::NONE, data.result);

        data.async_ref.Abort();
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
