#ifndef BENG_PROXY_HTTP_CACHE_INTERNAL_HXX
#define BENG_PROXY_HTTP_CACHE_INTERNAL_HXX

#include "http_cache.h"

#include <sys/types.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static const off_t cacheable_size_limit = 256 * 1024;

#endif
