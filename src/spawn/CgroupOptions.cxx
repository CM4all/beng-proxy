/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CgroupOptions.hxx"
#include "CgroupState.hxx"
#include "Config.hxx"
#include "mount_list.hxx"
#include "AllocatorPtr.hxx"
#include "system/pivot_root.h"
#include "system/bind_mount.h"
#include "io/WriteFile.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef __linux
#error This library requires Linux
#endif

CgroupOptions::CgroupOptions(AllocatorPtr alloc, const CgroupOptions &src)
    :name(alloc.CheckDup(src.name))
{
    auto **set_tail = &set_head;

    for (const auto *i = src.set_head; i != nullptr; i = i->next) {
        auto *new_set = alloc.New<SetItem>(alloc.Dup(i->name),
                                           alloc.Dup(i->value));
        *set_tail = new_set;
        set_tail = &new_set->next;
    }
}

void
CgroupOptions::Set(AllocatorPtr alloc, StringView _name, StringView _value)
{
    auto *new_set = alloc.New<SetItem>(alloc.DupZ(_name), alloc.DupZ(_value));
    new_set->next = set_head;
    set_head = new_set;
}

static void
WriteFile(const char *path, const char *data)
{
    if (TryWriteExistingFile(path, data) == WriteFileResult::ERROR) {
        fprintf(stderr, "write('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }
}

static void
MoveToNewCgroup(const char *mount_base_path, const char *controller,
                const char *delegated_group, const char *sub_group)
{
    char path[PATH_MAX];

    constexpr int max_path = sizeof(path) - 16;
    if (snprintf(path, max_path, "%s/%s%s/%s",
                 mount_base_path, controller,
                 delegated_group, sub_group) >= max_path) {
        fprintf(stderr, "Path is too long");
        _exit(2);
    }

    if (mkdir(path, 0777) < 0) {
        switch (errno) {
        case EEXIST:
            break;

        default:
            fprintf(stderr, "mkdir('%s') failed: %s\n",
                    path, strerror(errno));
            _exit(2);
        }
    }

    strcat(path, "/cgroup.procs");
    WriteFile(path, "0");
}

void
CgroupOptions::Apply(const CgroupState &state) const
{
    if (name == nullptr)
        return;

    if (!state.IsEnabled()) {
        fprintf(stderr, "Control groups are disabled\n");
        _exit(2);
    }

    const auto mount_base_path = "/sys/fs/cgroup";

    for (const auto &mount_point : state.mounts)
        MoveToNewCgroup(mount_base_path, mount_point.c_str(),
                        state.group_path.c_str(), name);

    // TODO: move to "name=systemd"?

    for (const auto *set = set_head; set != nullptr; set = set->next) {
        const char *dot = strchr(set->name, '.');
        assert(dot != nullptr);

        const std::string controller(set->name, dot);
        auto i = state.controllers.find(controller);
        if (i == state.controllers.end()) {
            fprintf(stderr, "cgroup controller '%s' is unavailable\n",
                    controller.c_str());
            _exit(2);
        }

        const std::string &mount_point = i->second;

        char path[PATH_MAX];

        if (snprintf(path, sizeof(path), "%s/%s%s/%s/%s",
                     mount_base_path, mount_point.c_str(),
                     state.group_path.c_str(), name,
                     set->name) >= (int)sizeof(path)) {
            fprintf(stderr, "Path is too long");
            _exit(2);
        }

        WriteFile(path, set->value);
    }
}

char *
CgroupOptions::MakeId(char *p) const
{
    if (name != nullptr) {
        p = (char *)mempcpy(p, ";cg", 3);
        p = stpcpy(p, name);
    }

    return p;
}
