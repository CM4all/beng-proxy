/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GotoConfig.hxx"

bool
LbBranchConfig::HasZeroConf() const
{
    if (fallback.HasZeroConf())
        return true;

    for (const auto &i : conditions)
        if (i.HasZeroConf())
            return true;

    return false;
}

LbProtocol
LbGotoConfig::GetProtocol() const
{
    assert(IsDefined());

    if (response.IsDefined() || lua != nullptr || translation != nullptr)
        return LbProtocol::HTTP;

    return cluster != nullptr
        ? cluster->protocol
        : branch->GetProtocol();
}

const char *
LbGotoConfig::GetName() const
{
    assert(IsDefined());

    if (lua != nullptr)
        return lua->name.c_str();

    if (translation != nullptr)
        return translation->name.c_str();

    return cluster != nullptr
        ? cluster->name.c_str()
        : branch->name.c_str();
}

bool
LbGotoConfig::HasZeroConf() const
{
    return (cluster != nullptr && cluster->HasZeroConf()) ||
        (branch != nullptr && branch->HasZeroConf());
}
