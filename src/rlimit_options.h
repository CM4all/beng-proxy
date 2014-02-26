/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RLIMIT_OPTIONS_H
#define BENG_PROXY_RLIMIT_OPTIONS_H

#include <sys/resource.h>
#include <stdbool.h>

#define RLIM_UNDEFINED ((rlim_t)-2)

/**
 * Resource limits.
 */
struct rlimit_options {
    struct rlimit values[RLIM_NLIMITS];
};

static inline void
rlimit_options_init(struct rlimit_options *r)
{
    for (unsigned i = 0; i < RLIM_NLIMITS; ++i) {
        r->values[i].rlim_cur = RLIM_UNDEFINED;
        r->values[i].rlim_max = RLIM_UNDEFINED;
    }
}

static inline void
rlimit_options_copy(struct rlimit_options *dest,
                    const struct rlimit_options *src)
{
    *dest = *src;
}

#ifdef __cplusplus
extern "C" {
#endif

char *
rlimit_options_id(const struct rlimit_options *r, char *p);

void
rlimit_options_apply(const struct rlimit_options *r);

bool
rlimit_options_parse(struct rlimit_options *r, const char *s);

#ifdef __cplusplus
}
#endif

#endif
