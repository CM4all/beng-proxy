/*
 * Format and parse HTTP dates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HTTP_DATE_HXX
#define HTTP_DATE_HXX

#include <inline/compiler.h>

#include <chrono>

void
http_date_format_r(char *buffer, std::chrono::system_clock::time_point t);

gcc_const
const char *
http_date_format(std::chrono::system_clock::time_point t);

gcc_pure
std::chrono::system_clock::time_point
http_date_parse(const char *p);

#endif
