/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef FILE_LINE_PARSER_HXX
#define FILE_LINE_PARSER_HXX

#include "LineParser.hxx"

#include <boost/filesystem.hpp>

class FileLineParser : public LineParser {
    const boost::filesystem::path &base_path;

public:
    FileLineParser(const boost::filesystem::path &_base_path, char *_p)
        :LineParser(_p), base_path(_base_path) {}

    boost::filesystem::path ExpectPath();
    boost::filesystem::path ExpectPathAndEnd();
};

#endif
