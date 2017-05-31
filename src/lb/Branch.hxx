/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_BRANCH_HXX
#define BENG_LB_BRANCH_HXX

#include "Goto.hxx"
#include "GotoConfig.hxx"

#include <list>

class LbGotoMap;
struct LbGotoIfConfig;
struct LbBranchConfig;

class LbGotoIf {
    const LbGotoIfConfig &config;

    const LbGoto destination;

public:
    LbGotoIf(LbGotoMap &goto_map, const LbGotoIfConfig &_config);

    const LbGotoIfConfig &GetConfig() const {
        return config;
    }

    template<typename R>
    gcc_pure
    bool MatchRequest(const R &request) const {
        return config.condition.MatchRequest(request);
    }

    const LbGoto &GetDestination() const {
        return destination;
    }
};

class LbBranch {
    const LbBranchConfig &config;

    LbGoto fallback;

    std::list<LbGotoIf> conditions;

public:
    LbBranch(LbGotoMap &goto_map, const LbBranchConfig &_config);

    const LbBranchConfig &GetConfig() const {
        return config;
    }

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const {
        for (const auto &i : conditions)
            if (i.MatchRequest(request))
                return i.GetDestination().FindRequestLeaf(request);

        return fallback.FindRequestLeaf(request);
    }
};

#endif
