/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACCESS_LOG_CONFIG_PARSER_HXX
#define ACCESS_LOG_CONFIG_PARSER_HXX

#include "Config.hxx"
#include "io/ConfigParser.hxx"

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
class AccessLogConfigParser : public ConfigParser {
    AccessLogConfig config;
    bool enabled = true, type_selected = false;

public:
    AccessLogConfig &&GetConfig() {
        return std::move(config);
    }

protected:
    /* virtual methods from class ConfigParser */
    void ParseLine(FileLineParser &line) override;
    void Finish() override;
};

#endif
