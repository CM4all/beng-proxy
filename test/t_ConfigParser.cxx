#include "io/ConfigParser.hxx"
#include "io/FileLineParser.hxx"
#include "util/ScopeExit.hxx"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <string.h>
#include <stdlib.h>

class MyConfigParser final
    : public ConfigParser, public std::vector<std::string> {
public:
    void ParseLine(FileLineParser &line) override {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("Quoted value expected");
        line.ExpectEnd();
        emplace_back(value);
    }
};

static void
ParseConfigFile(ConfigParser &parser, const char *const*lines)
{
    while (*lines != nullptr) {
        char *line = strdup(*lines++);
        AtScopeExit(line) { free(line); };

        FileLineParser line_parser({}, line);
        if (!parser.PreParseLine(line_parser))
            parser.ParseLine(line_parser);
    }

    parser.Finish();
}

static const char *const v_data[] = {
    "@set foo='bar'",
    "@set bar=\"${foo}\"",
    "${foo} ",
    "'${foo}'",
    "\"${foo}\"",
    "\"${bar}\"",
    " \"a${foo}b\" ",
    "@set foo=\"with space\"",
    "\"${foo}\"",
    "  ${foo}  ",
    nullptr
};

static const char *const v_output[] = {
    "bar",
    "${foo}",
    "bar",
    "bar",
    "abarb",
    "with space",
    "with space",
    nullptr
};

class ConfigParserTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ConfigParserTest);
    CPPUNIT_TEST(TestVariableConfigParser);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestVariableConfigParser() {
        MyConfigParser p;
        VariableConfigParser v(p);

        ParseConfigFile(v, v_data);

        for (size_t i = 0; v_output[i] != nullptr; ++i) {
            CPPUNIT_ASSERT(i < p.size());
            CPPUNIT_ASSERT(strcmp(v_output[i], p[i].c_str()) == 0);
        }
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ConfigParserTest);

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
