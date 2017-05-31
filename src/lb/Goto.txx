/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_GOTO_TXX
#define BENG_LB_GOTO_TXX

#include "Goto.hxx"
#include "Branch.hxx"

template<typename R>
const LbGoto &
LbGoto::FindRequestLeaf(const R &request) const
{
    if (branch != nullptr)
        return branch->FindRequestLeaf(request);

    return *this;
}

#endif
