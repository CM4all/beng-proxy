/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_DELEGATE_ADDRESS_HXX
#define BENG_PROXY_DELEGATE_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"

#include "util/Compiler.h"

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
    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};

#endif
