/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CgroupOptions.hxx"
#include "CgroupState.hxx"
#include "Config.hxx"
#include "mount_list.hxx"
#include "pool.hxx"
#include "system/pivot_root.h"
#include "system/bind_mount.h"
#include "pexpand.hxx"

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef __linux
#error This library requires Linux
#endif

CgroupOptions::CgroupOptions(struct pool &pool, const CgroupOptions &src)
    :name(p_strdup_checked(&pool, src.name))
{
}

static void
WriteFile(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY|O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    if (write(fd, data, strlen(data)) < 0) {
        fprintf(stderr, "write('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    close(fd);
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
