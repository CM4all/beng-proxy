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
VariableConfigParser::PreParseLine(LineParser &line)
{
    return child.PreParseLine(line);
}

void
VariableConfigParser::ParseLine(LineParser &line)
{
    Expand(line);

    if (line.SkipWord("@set")) {
        const char *name = line.ExpectWordAndSymbol('=',
                                                    "Variable name expected",
                                                    "'=' expected");
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("Quoted value expected after '='");

        line.ExpectEnd();

        auto i = variables.emplace(name, value);
        if (!i.second)
            i.first->second = value;
    } else {
        child.ParseLine(line);
    }
}

void
VariableConfigParser::Finish()
{
    child.Finish();
    ConfigParser::Finish();
}

void
VariableConfigParser::ExpandOne(std::string &dest,
                                const char *&src, const char *end) const
{
    assert(src + 2 <= end);
    assert(*src == '$');
    assert(src[1] == '{');

    src += 2;

    if (src >= end || !LineParser::IsWordChar(*src))
        throw LineParser::Error("Variable name expected after '${'");

    const char *name_begin = src++;

    do {
        if (++src >= end)
            throw LineParser::Error("Missing '}' after variable name");
    } while (LineParser::IsWordChar(*src));

    if (*src != '}')
        throw LineParser::Error("Missing '}' after variable name");

    const char *name_end = src++;

    const std::string name(name_begin, name_end);
    auto i = variables.find(name);
    if (i == variables.end())
        throw LineParser::Error("No such variable: " + name);

    dest += i->second;
}

void
VariableConfigParser::ExpandQuoted(std::string &dest,
                                   const char *src, const char *end) const
{
    while (true) {
        const char *dollar = (const char *)memchr(src, '$', end - src);
        if (dollar == nullptr)
            break;

        dest.append(src, dollar);

        src = dollar;
        ExpandOne(dest, src, end);
    }

    dest.append(src, end);
}

void
VariableConfigParser::Expand(std::string &dest, const char *src) const
{
    while (true) {
        const char ch = *src;
        if (ch == 0)
            break;

        if (ch == '\'') {
            const char *end = strchr(src + 1, '\'');
            if (end == nullptr)
                break;

            ++end;
            dest.append(src, end);
            src = end;
        } else if (ch == '"') {
            const char *end = strchr(src + 1, '"');
            if (end == nullptr)
                break;

            dest.push_back(ch);
            ExpandQuoted(dest, src + 1, end);
            dest.push_back(ch);
            src = end + 1;
        } else if (ch == '$' && src[1] == '{') {
            dest.push_back('\'');
            ExpandOne(dest, src, src + strlen(src));
            dest.push_back('\'');
        } else {
            dest.push_back(ch);
            ++src;
        }
    }

    dest += src;
}

char *
VariableConfigParser::Expand(const char *src) const
{
    if (strstr(src, "${") == nullptr)
        return nullptr;

    buffer.clear();
    Expand(buffer, src);
    return &buffer.front();
}

void
VariableConfigParser::Expand(LineParser &line) const
{
    char *p = Expand(line.Rest());
    if (p != nullptr)
        line.Replace(p);
}

bool
IncludeConfigParser::PreParseLine(LineParser &line)
{
    return child.PreParseLine(line);
}

void
IncludeConfigParser::ParseLine(LineParser &line)
{
    if (line.SkipWord("@include") ||
        /* v11.2 legacy: */ line.SkipWord("include")) {
        const char *p = line.NextUnescape();
        if (p == nullptr)
            throw LineParser::Error("Quoted path expected");

        line.ExpectEnd();

        IncludePath(p);
    } else if (line.SkipWord("@include_optional") ||
               /* v11.2 legacy: */ line.SkipWord("include_optional")) {
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
