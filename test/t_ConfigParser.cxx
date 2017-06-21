#include "io/ConfigParser.hxx"
#include "io/FileLineParser.hxx"
#include "util/ScopeExit.hxx"

#include <inline/compiler.h>

#include <gtest/gtest.h>

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

TEST(ConfigParserTest, VariableConfigParser)
{
    MyConfigParser p;
    VariableConfigParser v(p);

    ParseConfigFile(v, v_data);

    for (size_t i = 0; v_output[i] != nullptr; ++i) {
        ASSERT_LT(i, p.size());
        ASSERT_STREQ(v_output[i], p[i].c_str());
    }
}
