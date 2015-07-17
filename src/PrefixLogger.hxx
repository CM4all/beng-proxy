/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PREFIX_LOGGER_HXX
#define BENG_PROXY_PREFIX_LOGGER_HXX

#include <utility>

class Error;
class PrefixLogger;

std::pair<PrefixLogger *, int>
CreatePrefixLogger(Error &error);

void
DeletePrefixLogger(PrefixLogger *pl);

void
PrefixLoggerSetPrefix(PrefixLogger &pl, const char *prefix);

void
PrefixLoggerSetPid(PrefixLogger &pl, int pid);

#endif
