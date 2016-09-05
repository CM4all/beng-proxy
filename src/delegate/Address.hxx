/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_ADDRESS_HXX
#define BENG_PROXY_DELEGATE_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"

#include <inline/compiler.h>

struct pool;
class MatchInfo;
class Error;

/**
 * The description of a delegate process.
 */
struct DelegateAddress {
    const char *delegate;

    /**
     * Options for the delegate process.
     */
    ChildOptions child_options;

    DelegateAddress(const char *_delegate);

    constexpr DelegateAddress(ShallowCopy shallow_copy,
                              const DelegateAddress &src)
        :delegate(src.delegate),
         child_options(shallow_copy, src.child_options) {}

    DelegateAddress(struct pool &pool, const DelegateAddress &src);

    bool Check(GError **error_r) const {
        return child_options.Check(error_r);
    }

    /**
     * Does this object need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return child_options.IsExpandable();
    }

    bool Expand(struct pool &pool, const MatchInfo &match_info,
                Error &error_r);
};

#endif
