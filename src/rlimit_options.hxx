/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RLIMIT_OPTIONS_HXX
#define BENG_PROXY_RLIMIT_OPTIONS_HXX

#include <sys/resource.h>

#define RLIM_UNDEFINED ((rlim_t)-2)

struct ResourceLimit : rlimit {
    void Init() {
        rlim_cur = RLIM_UNDEFINED;
        rlim_max = RLIM_UNDEFINED;
    }

    constexpr bool IsEmpty() const {
        return rlim_cur == RLIM_UNDEFINED && rlim_max == RLIM_UNDEFINED;
    }

    constexpr bool IsFull() const {
        return rlim_cur != RLIM_UNDEFINED && rlim_max != RLIM_UNDEFINED;
    }

    void Get(int resource);
    bool Set(int resource) const;

    void OverrideFrom(const ResourceLimit &src);
    void CompleteFrom(int resource, const ResourceLimit &src);
};

/**
 * Resource limits.
 */
struct rlimit_options {
    ResourceLimit values[RLIM_NLIMITS];

    void Init() {
        for (auto &i : values)
            i.Init();
    }

    bool IsEmpty() const;

    unsigned GetHash() const;

    char *MakeId(char *p) const;

    void Apply() const;

    bool Parse(const char *s);
};

#endif
