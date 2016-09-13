/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGROUP_OPTIONS_HXX
#define BENG_PROXY_CGROUP_OPTIONS_HXX

#include <inline/compiler.h>

class AllocatorPtr;
struct StringView;
struct CgroupState;

struct CgroupOptions {
    const char *name = nullptr;

    struct SetItem {
        SetItem *next = nullptr;
        const char *const name;
        const char *const value;

        constexpr SetItem(const char *_name, const char *_value)
            :name(_name), value(_value) {}
    };

    SetItem *set_head = nullptr;

    CgroupOptions() = default;
    CgroupOptions(AllocatorPtr alloc, const CgroupOptions &src);

    void Set(AllocatorPtr alloc, StringView name, StringView value);

    void Apply(const CgroupState &state) const;

    char *MakeId(char *p) const;
};

#endif
