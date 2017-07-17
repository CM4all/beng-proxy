/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConfigParser.hxx"
#include "io/FileLineParser.hxx"

void
AccessLogConfigParser::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "enabled") == 0) {
        enabled = line.NextBool();
        line.ExpectEnd();
    } else if (strcmp(word, "shell") == 0) {
        if (type_selected)
            throw LineParser::Error("Access logger already defined");

        type_selected = true;
        config.type = AccessLogConfig::Type::EXECUTE;
        config.command = line.ExpectValueAndEnd();
    } else if (strcmp(word, "ignore_localhost_200") == 0) {
        config.ignore_localhost_200 = line.ExpectValueAndEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
AccessLogConfigParser::Finish()
{
    if (!enabled) {
        config.type = AccessLogConfig::Type::DISABLED;
        type_selected = true;
    }

    if (!type_selected)
        throw std::runtime_error("Empty access_logger block");
}
