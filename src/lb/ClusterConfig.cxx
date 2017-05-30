/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ClusterConfig.hxx"

void
LbClusterConfig::FillAddressList()
{
    assert(address_list.IsEmpty());

    address_list.SetStickyMode(sticky_mode);

    for (auto &member : members) {
        address_allocations.emplace_front(member.node->address);
        auto &address = address_allocations.front();
        if (member.port != 0)
            address.SetPort(member.port);

        if (!address_list.AddPointer(address))
            throw std::runtime_error("Too many members");
    }
}

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
