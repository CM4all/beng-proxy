/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_ONE_LINE_HXX
#define BENG_PROXY_LOG_ONE_LINE_HXX

struct AccessLogDatagram;

/**
 * Print the #AccessLogDatagram in one line, similar to Apache's
 * "combined" log format.
 */
void
LogOneLine(const AccessLogDatagram &d);

#endif
