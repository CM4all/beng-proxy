/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RLIMIT_OPTIONS_HXX
#define BENG_PROXY_RLIMIT_OPTIONS_HXX

#include <sys/resource.h>

#define RLIM_UNDEFINED ((rlim_t)-2)

/**
 * Resource limits.
 */
struct rlimit_options {
    struct rlimit values[RLIM_NLIMITS];

    void Init() {
        for (unsigned i = 0; i < RLIM_NLIMITS; ++i) {
            values[i].rlim_cur = RLIM_UNDEFINED;
            values[i].rlim_max = RLIM_UNDEFINED;
        }
    }

    bool IsEmpty() const;

    unsigned GetHash() const;

    char *MakeId(char *p) const;

    void Apply() const;

    bool Parse(const char *s);
};

#endif
