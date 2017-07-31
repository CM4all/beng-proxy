#include "regex.hxx"
#include "pexpand.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include "util/Compiler.h"

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <string.h>
#include <stdlib.h>

class RegexTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(RegexTest);
    CPPUNIT_TEST(TestMatch1);
    CPPUNIT_TEST(TestMatch2);
    CPPUNIT_TEST(TestNotAnchored);
    CPPUNIT_TEST(TestAnchored);
    CPPUNIT_TEST(TestExpand);
    CPPUNIT_TEST(TestExpandMalformedUriEscape);
    CPPUNIT_TEST(TestExpandOptional);
    CPPUNIT_TEST(TestExpandOptionalLast);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestMatch1() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile(".", false, false);
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("a"));
        CPPUNIT_ASSERT(r.Match("abc"));
    }

    void TestMatch2() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("..", false, false);
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(!r.Match("a"));
        CPPUNIT_ASSERT(r.Match("abc"));
    }

    void TestNotAnchored() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("/foo/", false, false);
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("/foo/"));
        CPPUNIT_ASSERT(r.Match("/foo/bar"));
        CPPUNIT_ASSERT(r.Match("foo/foo/"));
    }

    void TestAnchored() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("/foo/", true, false);
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("/foo/"));
        CPPUNIT_ASSERT(r.Match("/foo/bar"));
        CPPUNIT_ASSERT(!r.Match("foo/foo/"));
    }

    void TestExpand() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("^/foo/(\\w+)/([^/]+)/(.*)$", false, true);
        CPPUNIT_ASSERT(r.IsDefined());

        CPPUNIT_ASSERT(!r.Match("a"));

        auto match_info = r.MatchCapture("/foo/bar/a/b/c.html");
        CPPUNIT_ASSERT(match_info.IsDefined());

        TestPool pool;
        AllocatorPtr alloc(pool);

        auto e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a-b/c.html-\\") == 0);

        match_info = r.MatchCapture("/foo/bar/a/b/");
        CPPUNIT_ASSERT(match_info.IsDefined());

        e = expand_string(alloc, "\\1-\\2-\\3-\\\\", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a-b/-\\") == 0);

        match_info = r.MatchCapture("/foo/bar/a%20b/c%2520.html");
        CPPUNIT_ASSERT(match_info.IsDefined());

        e = expand_string_unescaped(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a b-c%20.html") == 0);

        try {
            e = expand_string_unescaped(alloc, "\\4", match_info);
            CPPUNIT_FAIL("Must fail");
        } catch (...) {
        }
    }

    void TestExpandMalformedUriEscape() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("^(.*)$", false, true);
        CPPUNIT_ASSERT(r.IsDefined());

        auto match_info = r.MatchCapture("%xxx");
        CPPUNIT_ASSERT(match_info.IsDefined());

        TestPool pool;
        AllocatorPtr alloc(pool);

        auto e = expand_string(alloc, "-\\1-", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "-%xxx-") == 0);

        try {
            e = expand_string_unescaped(alloc, "-\\1-", match_info);
            CPPUNIT_FAIL("Must fail");
        } catch (...) {
        }
    }

    void TestExpandOptional() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("^(a)(b)?(c)$", true, true);
        CPPUNIT_ASSERT(r.IsDefined());

        auto match_info = r.MatchCapture("abc");
        CPPUNIT_ASSERT(match_info.IsDefined());

        TestPool pool;
        AllocatorPtr alloc(pool);

        auto e = expand_string(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "a-b-c") == 0);

        match_info = r.MatchCapture("ac");
        CPPUNIT_ASSERT(match_info.IsDefined());
        e = expand_string(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "a--c") == 0);
    }

    void TestExpandOptionalLast() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        r.Compile("^(a)(b)?(c)?$", true, true);
        CPPUNIT_ASSERT(r.IsDefined());

        auto match_info = r.MatchCapture("abc");
        CPPUNIT_ASSERT(match_info.IsDefined());

        TestPool pool;
        AllocatorPtr alloc(pool);

        auto e = expand_string(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "a-b-c") == 0);

        match_info = r.MatchCapture("ac");
        CPPUNIT_ASSERT(match_info.IsDefined());
        e = expand_string(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "a--c") == 0);

        match_info = r.MatchCapture("ab");
        CPPUNIT_ASSERT(match_info.IsDefined());
        e = expand_string(alloc, "\\1-\\2-\\3", match_info);
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "a-b-") == 0);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(RegexTest);

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
