/*
 * Copyright 2007-2019 Content Management AG
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
                 FailureStatus new_status,
                 std::chrono::seconds duration) noexcept
{
    switch (new_status) {
    case FailureStatus::OK:
        break;

    case FailureStatus::FADE:
        SetFade(now, duration);
        break;

    case FailureStatus::PROTOCOL:
        SetProtocol(now, duration);
        break;

    case FailureStatus::CONNECT:
        SetConnect(now, duration);
        break;

    case FailureStatus::MONITOR:
        SetMonitor();
        break;
    }
}

void
FailureInfo::Unset(FailureStatus unset_status) noexcept
{
    switch (unset_status) {
    case FailureStatus::OK:
        UnsetAll();
        break;

    case FailureStatus::FADE:
        UnsetFade();
        break;

    case FailureStatus::PROTOCOL:
        UnsetProtocol();
        break;

    case FailureStatus::CONNECT:
        UnsetConnect();
        break;

    case FailureStatus::MONITOR:
        UnsetMonitor();
        break;
    }
}
