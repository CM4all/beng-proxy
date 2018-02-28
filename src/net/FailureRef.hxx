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

#pragma once

#include "FailureInfo.hxx"
#include "util/Compiler.h"

class Expiry;
class FailureInfo;

class ReferencedFailureInfo : public FailureInfo {
    unsigned refs = 1;

public:
    using FailureInfo::FailureInfo;

    bool IsNull() const noexcept {
        return FailureInfo::IsNull() && refs == 0;
    }

    void Ref() noexcept {
        ++refs;
    }

    void Unref() noexcept {
        if (--refs == 0)
            Destroy();
    }

    struct UnrefDisposer {
        void operator()(ReferencedFailureInfo *failure) const noexcept {
            failure->Unref();
        }
    };

protected:
    virtual void Destroy() = 0;
};

/**
 * Holds a (counted) reference to a #FailureInfo instance.
 */
class FailureRef {
    ReferencedFailureInfo &info;

public:
    explicit FailureRef(ReferencedFailureInfo &_info) noexcept;
    ~FailureRef() noexcept;

    FailureRef(const FailureRef &) = delete;
    FailureRef &operator=(const FailureRef &) = delete;

    FailureInfo *operator->() {
        return &info;
    }

    FailureInfo &operator*() {
        return info;
    }
};

/**
 * Like #FailureRef, but manages a dynamic pointer.
 */
class FailurePtr {
    ReferencedFailureInfo *info = nullptr;

public:
    FailurePtr() = default;

    ~FailurePtr() noexcept {
        if (info != nullptr)
            info->Unref();
    }

    FailurePtr(const FailurePtr &) = delete;
    FailurePtr &operator=(const FailurePtr &) = delete;

    operator bool() const {
        return info != nullptr;
    }

    FailurePtr &operator=(ReferencedFailureInfo &new_info) noexcept {
        if (info != nullptr)
            info->Unref();
        info = &new_info;
        info->Ref();
        return *this;
    }

    FailureInfo *operator->() {
        return info;
    }

    FailureInfo &operator*() {
        return *info;
    }
};
