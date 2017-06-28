/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LOGGER_HXX
#define LOGGER_HXX

#include <daemon/log.h>

#include <exception>
#include <string>

class Logger {
    mutable std::string log_name;

public:
    const std::string &GetLogName() const noexcept {
        if (log_name.empty())
            log_name = MakeLogName();
        return log_name;
    }

    void ResetLogName() {
        log_name.clear();
    }

    static bool IsLogLevelVisible(int level) {
        return daemon_log_config.verbose >= level;
    }

    void Log(int level, const char *msg) const {
        daemon_log(level, "[%s] %s\n", GetLogName().c_str(), msg);
    }

    void LogPrefix(int level, const char *prefix, const char *msg) const {
        daemon_log(level, "[%s] %s: %s\n", GetLogName().c_str(), prefix, msg);
    }

    void Log(int level, const char *prefix, const std::exception &e) const;
    void Log(int level, const char *prefix, std::exception_ptr ep) const;
    void LogErrno(int level, const char *prefix, int e) const;

protected:
    virtual std::string MakeLogName() const noexcept = 0;
};

#endif
