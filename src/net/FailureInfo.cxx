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

#include "FailureInfo.hxx"

void
FailureInfo::Set(Expiry now,
                 enum failure_status new_status,
                 std::chrono::seconds duration) noexcept
{
    switch (new_status) {
    case FAILURE_OK:
        break;

    case FAILURE_FADE:
        SetFade(now, duration);
        break;

    case FAILURE_PROTOCOL:
        SetProtocol(now, duration);
        break;

    case FAILURE_CONNECT:
        SetConnect(now, duration);
        break;

    case FAILURE_MONITOR:
        SetMonitor();
        break;
    }
}

void
FailureInfo::Unset(enum failure_status unset_status) noexcept
{
    switch (unset_status) {
    case FAILURE_OK:
        UnsetAll();
        break;

    case FAILURE_FADE:
        UnsetFade();
        break;

    case FAILURE_PROTOCOL:
        UnsetProtocol();
        break;

    case FAILURE_CONNECT:
        UnsetConnect();
        break;

    case FAILURE_MONITOR:
        UnsetMonitor();
        break;
    }
}
