/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Logger.hxx"
#include "util/Exception.hxx"

#include <glib.h>

#include <string.h>

void
Logger::Log(int level, const char *prefix, const GError *error) const
{
    LogPrefix(level, prefix, error->message);
}

void
Logger::Log(int level, const char *prefix, const std::exception &e) const
{
    if (IsLogLevelVisible(level))
        LogPrefix(level, prefix, GetFullMessage(e).c_str());
}

void
Logger::Log(int level, const char *prefix, std::exception_ptr ep) const
{
    if (IsLogLevelVisible(level))
        LogPrefix(level, prefix, GetFullMessage(ep).c_str());
}

void
Logger::LogErrno(int level, const char *prefix, int e) const
{
    if (IsLogLevelVisible(level))
        LogPrefix(level, prefix, strerror(e));
}
