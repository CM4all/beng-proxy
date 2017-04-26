/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FileLineParser.hxx"

namespace fs = boost::filesystem;

static fs::path
ApplyPath(const fs::path &base, fs::path &&p)
{
    if (p.is_absolute())
        /* is already absolute */
        return p;

    return base.parent_path() / p;
}

fs::path
FileLineParser::ExpectPath()
{
    const char *value = NextUnescape();
    if (value == nullptr)
        throw Error("Quoted path expected");

    return ApplyPath(base_path, value);
}

fs::path
FileLineParser::ExpectPathAndEnd()
{
    auto value = ExpectPath();
    ExpectEnd();
    return value;
}
