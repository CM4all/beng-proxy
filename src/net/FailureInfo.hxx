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

#ifndef FAILURE_INFO_HXX
#define FAILURE_INFO_HXX

#include "FailureStatus.hxx"
#include "util/Expiry.hxx"
#include "util/Compiler.h"

class FailureInfo {
    Expiry expires;

    Expiry fade_expires = Expiry::AlreadyExpired();

    enum failure_status status;

public:
    constexpr FailureInfo(enum failure_status _status,
                          Expiry _expires) noexcept
        :expires(_expires), status(_status) {}

    constexpr bool IsNull() const noexcept {
        return expires == Expiry::AlreadyExpired() &&
            fade_expires == Expiry::AlreadyExpired();
    }

private:
    constexpr bool CanExpire() const noexcept {
        return status != FAILURE_MONITOR;
    }

    constexpr bool IsExpired(Expiry now) const noexcept {
        return CanExpire() && expires.IsExpired(now);
    }

    constexpr bool IsFade(Expiry now) const noexcept {
        return !fade_expires.IsExpired(now);
    }

public:
    constexpr enum failure_status GetStatus(Expiry now) const noexcept {
        if (!IsExpired(now))
            return status;
        else if (IsFade(now))
            return FAILURE_FADE;
        else
            return FAILURE_OK;
    }

    /**
     * Set the specified failure status, but only if it is not less
     * severe than the current status.
     *
     * @return false if the new status is less severe, and nothing has
     * changed
     */
    bool Set(Expiry now, enum failure_status new_status,
             std::chrono::seconds duration) noexcept;

    void Unset(Expiry now, enum failure_status unset_status) noexcept;
};

#endif
