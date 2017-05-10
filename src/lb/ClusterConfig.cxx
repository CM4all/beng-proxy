/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ClusterConfig.hxx"

int
LbClusterConfig::FindJVMRoute(const char *jvm_route) const
{
    assert(jvm_route != nullptr);

    for (unsigned i = 0, n = members.size(); i < n; ++i) {
        const auto &node = *members[i].node;

        if (!node.jvm_route.empty() && node.jvm_route == jvm_route)
            return i;
    }

    return -1;
}
