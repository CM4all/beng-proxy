/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_ADDRESS_HXX
#define BENG_PROXY_DELEGATE_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"

#include <inline/compiler.h>

class AllocatorPtr;
class MatchInfo;

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

    constexpr DelegateAddress(DelegateAddress &&src)
        :DelegateAddress(ShallowCopy(), src) {}

    DelegateAddress(AllocatorPtr alloc, const DelegateAddress &src);

    DelegateAddress &operator=(const DelegateAddress &) = delete;

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const {
        child_options.Check();
    }

    /**
     * Does this object need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return child_options.IsExpandable();
    }

    /**
     * Throws std::runtime_error on error.
     */
    void Expand(struct pool &pool, const MatchInfo &match_info);
};

#endif
