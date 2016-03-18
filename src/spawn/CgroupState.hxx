/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGROUP_STATE_HXX
#define BENG_PROXY_CGROUP_STATE_HXX

#include <forward_list>
#include <string>

struct CgroupState {
    /**
     * Our own control group path.  It starts with a slash.
     */
    std::string group_path;

    /**
     * The controllers which are managed by us (delegated from
     * systemd).
     */
    std::forward_list<std::string> controllers;

    bool IsEnabled() const {
        return !group_path.empty();
    }
};

#endif
