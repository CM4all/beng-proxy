/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_LIMITS_HXX
#define BENG_PROXY_RESOURCE_LIMITS_HXX

#include <sys/resource.h>

struct ResourceLimit : rlimit {
    static constexpr rlim_t UNDEFINED = rlim_t(-2);

    constexpr ResourceLimit()
        :rlimit{UNDEFINED, UNDEFINED} {}

    constexpr bool IsEmpty() const {
        return rlim_cur == UNDEFINED && rlim_max == UNDEFINED;
    }

    constexpr bool IsFull() const {
        return rlim_cur != UNDEFINED && rlim_max != UNDEFINED;
    }

    void Get(int resource);
    bool Set(int resource) const;

    void OverrideFrom(const ResourceLimit &src);
    void CompleteFrom(int resource, const ResourceLimit &src);
};

/**
 * Resource limits.
 */
struct ResourceLimits {
    ResourceLimit values[RLIM_NLIMITS];

    bool IsEmpty() const;

    unsigned GetHash() const;

    char *MakeId(char *p) const;

    void Apply() const;

    bool Parse(const char *s);
};

#endif
