/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGROUP_STATE_HXX
#define BENG_PROXY_CGROUP_STATE_HXX

#include <forward_list>
#include <string>
#include <map>

struct CgroupState {
    /**
     * Our own control group path.  It starts with a slash.
     */
    std::string group_path;

    /**
     * The controller mount points below /sys/fs/cgroup which are
     * managed by us (delegated from systemd).  Each mount point may
     * contain several controllers.
     */
    std::forward_list<std::string> mounts;

    /**
     * A mapping from controller name to mount point name.  More than
     * one controller may be mounted at one mount point.
     */
    std::map<std::string, std::string> controllers;

    bool IsEnabled() const {
        return !group_path.empty();
    }
};

#endif
