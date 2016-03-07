/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGROUP_OPTIONS_HXX
#define BENG_PROXY_CGROUP_OPTIONS_HXX

#include <inline/compiler.h>

struct pool;
struct CgroupState;

struct CgroupOptions {
    const char *name = nullptr;

    CgroupOptions() = default;
    CgroupOptions(struct pool &pool, const CgroupOptions &src);

    void Apply(const CgroupState &state) const;

    char *MakeId(char *p) const;
};

#endif
