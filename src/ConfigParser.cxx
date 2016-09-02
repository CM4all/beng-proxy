/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConfigParser.hxx"
#include "LineParser.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <errno.h>
#include <fnmatch.h>

namespace fs = boost::filesystem;

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

bool
IncludeConfigParser::PreParseLine(LineParser &line)
{
    return child.PreParseLine(line);
}

void
IncludeConfigParser::ParseLine(LineParser &line)
{
    if (line.SkipWord("include")) {
        const char *p = line.NextUnescape();
        if (p == nullptr)
            throw LineParser::Error("Quoted path expected");

        line.ExpectEnd();

        IncludePath(p);
    } else if (line.SkipWord("include_optional")) {
        const char *p = line.NextUnescape();
        if (p == nullptr)
            throw LineParser::Error("Quoted path expected");

        line.ExpectEnd();

        IncludeOptionalPath(p);
    } else
        child.ParseLine(line);
}

void
IncludeConfigParser::Finish()
{
    child.Finish();
}

static fs::path
ApplyPath(const fs::path &base, fs::path &&p)
{
    if (p.is_absolute())
        /* is already absolute */
        return p;

    return base.parent_path() / p;
}

inline void
IncludeConfigParser::IncludePath(boost::filesystem::path &&p)
{
    p = ApplyPath(path, std::move(p));

    auto directory = p.parent_path();
    if (directory.empty())
        directory = ".";

    const auto pattern = p.filename();

    if (pattern.native().find('*') != std::string::npos ||
        pattern.native().find('?') != std::string::npos) {

        std::vector<fs::path> files;

        /* range-based for requires Boost 1.56 */
        for (auto i = fs::directory_iterator(directory);
             i != fs::directory_iterator(); ++i)
            if (fnmatch(pattern.c_str(), i->path().filename().c_str(), 0) == 0)
                files.emplace_back(i->path());

        std::sort(files.begin(), files.end());

        for (auto &i : files) {
            IncludeConfigParser sub(std::move(i), child);
            ParseConfigFile(sub.path.c_str(), sub);
        }
    } else {
        IncludeConfigParser sub(std::move(p), child);
        ParseConfigFile(sub.path.c_str(), sub);
    }
}

static void
ParseConfigFile(const boost::filesystem::path &path, FILE *file,
                ConfigParser &parser)
{
    char buffer[4096], *line;
    unsigned i = 1;
    while ((line = fgets(buffer, sizeof(buffer), file)) != nullptr) {
        LineParser line_parser(line);

        try {
            if (!parser.PreParseLine(line_parser))
                parser.ParseLine(line_parser);
        } catch (...) {
            std::throw_with_nested(LineParser::Error(path.native() + ':' + std::to_string(i)));
        }

        ++i;
    }
}

inline void
IncludeConfigParser::IncludeOptionalPath(boost::filesystem::path &&p)
{
    IncludeConfigParser sub(ApplyPath(path, std::move(p)), child);

    FILE *file = fopen(sub.path.c_str(), "r");
    if (file == nullptr) {
        int e = errno;
        switch (e) {
        case ENOENT:
        case ENOTDIR:
            /* silently ignore this error */
            return;

        default:
            throw FormatErrno(e, "Failed to open %s", sub.path.c_str());
        }
    }

    AtScopeExit(file) { fclose(file); };

    ParseConfigFile(sub.path.c_str(), file, sub);
    sub.Finish();
}

void
ParseConfigFile(const boost::filesystem::path &path, ConfigParser &parser)
{
    FILE *file = fopen(path.c_str(), "r");
    if (file == nullptr)
        throw FormatErrno("Failed to open %s", path.c_str());

    AtScopeExit(file) { fclose(file); };

    ParseConfigFile(path, file, parser);
    parser.Finish();
}
