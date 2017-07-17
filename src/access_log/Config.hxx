/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACCESS_LOG_CONFIG_HXX
#define ACCESS_LOG_CONFIG_HXX

#include <string>
#include <set>

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
struct AccessLogConfig {
    enum class Type {
        DISABLED,
        INTERNAL,
        EXECUTE,
    } type = Type::INTERNAL;

    /**
     * A command to be executed with a shell, where fd0 is a socket
     * which receives access log datagrams.
     *
     * Special value "null" specifies that access logging is disabled
     * completely, and "" (empty string) specifies that one-line
     * logging is performed directly to stdandard output.
     */
    std::string command;

    /**
     * Don't log this request URI if host=="localhost" and
     * status==200.
     */
    std::string ignore_localhost_200;

    /**
     * A list of proxy servers whose "X-Forwarded-For" header will be
     * trusted.
     */
    std::set<std::string> trust_xff;

    /**
     * Setter for the deprecated "--access-logger" command-line
     * option, which has a few special cases.
     */
    void SetLegacy(const char *new_value) {
        command = new_value;

        if (command.empty() || command == "internal")
            type = Type::INTERNAL;
        else if (command == "null")
            type = Type::DISABLED;
        else
            type = Type::EXECUTE;
    }
};

#endif
