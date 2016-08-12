/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConfigParser.hxx"
#include "LineParser.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"

#include <inline/compiler.h>

#include <assert.h>

bool
ConfigParser::PreParseLine(gcc_unused LineParser &line)
{
    return false;
}

bool
NestedConfigParser::PreParseLine(LineParser &line)
{
    if (child) {
        if (child->PreParseLine(line))
            return true;

        if (line.SkipSymbol('}')) {
            line.ExpectEnd();
            child->Finish();
            child.reset();
            return true;
        }
    }

    return ConfigParser::PreParseLine(line);
}

void
NestedConfigParser::ParseLine(LineParser &line)
{
    if (child)
        child->ParseLine(line);
    else
        ParseLine2(line);
}

void
NestedConfigParser::Finish()
{
    if (child)
        throw LineParser::Error("Block not closed at end of file");

    ConfigParser::Finish();
}

void
NestedConfigParser::SetChild(std::unique_ptr<ConfigParser> &&_child)
{
    assert(!child);

    child = std::move(_child);
}

bool
CommentConfigParser::PreParseLine(LineParser &line)
{
    if (child.PreParseLine(line))
        return true;

    if (line.front() == '#' || line.IsEnd())
        /* ignore empty lines and comments */
        return true;

    return ConfigParser::PreParseLine(line);
}

void
CommentConfigParser::ParseLine(LineParser &line)
{
    child.ParseLine(line);
}

void
CommentConfigParser::Finish()
{
    child.Finish();
    ConfigParser::Finish();
}

static void
ParseConfigFile(const char *path, FILE *file, ConfigParser &parser)
{
    char buffer[4096], *line;
    unsigned i = 1;
    while ((line = fgets(buffer, sizeof(buffer), file)) != nullptr) {
        LineParser line_parser(line);

        try {
            if (!parser.PreParseLine(line_parser))
                parser.ParseLine(line_parser);
        } catch (...) {
            std::throw_with_nested(LineParser::Error(std::string(path) + ':' + std::to_string(i)));
        }

        ++i;
    }
}

void
ParseConfigFile(const char *path, ConfigParser &parser)
{
    FILE *file = fopen(path, "r");
    if (file == nullptr)
        throw FormatErrno("Failed to open %s", path);

    AtScopeExit(file) { fclose(file); };

    ParseConfigFile(path, file, parser);
    parser.Finish();
}
