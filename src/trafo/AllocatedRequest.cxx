/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#include "AllocatedRequest.hxx"
#include "translation/Protocol.hxx"

#include "util/Compiler.h"
#include <daemon/log.h>

#include <assert.h>

void
AllocatedTrafoRequest::Parse(TranslationCommand cmd,
                             const void *payload, size_t length)
{
    switch (cmd) {
    case TranslationCommand::BEGIN:
        Clear();

        if (length >= 1)
            protocol_version = *(const uint8_t *)payload;

        break;

    case TranslationCommand::END:
        assert(false);
        gcc_unreachable();

    case TranslationCommand::URI:
        uri_buffer.assign((const char *)payload, length);
        uri = uri_buffer.c_str();
        break;

    case TranslationCommand::HOST:
        host_buffer.assign((const char *)payload, length);
        host = host_buffer.c_str();
        break;

    case TranslationCommand::ARGS:
        args_buffer.assign((const char *)payload, length);
        args = args_buffer.c_str();
        break;

    case TranslationCommand::QUERY_STRING:
        query_string_buffer.assign((const char *)payload, length);
        query_string = query_string_buffer.c_str();
        break;

    case TranslationCommand::USER_AGENT:
        user_agent_buffer.assign((const char *)payload, length);
        user_agent = user_agent_buffer.c_str();
        break;

    case TranslationCommand::UA_CLASS:
        ua_class_buffer.assign((const char *)payload, length);
        ua_class = ua_class_buffer.c_str();
        break;

    case TranslationCommand::LANGUAGE:
        accept_language_buffer.assign((const char *)payload, length);
        accept_language = accept_language_buffer.c_str();
        break;

    case TranslationCommand::AUTHORIZATION:
        authorization_buffer.assign((const char *)payload, length);
        authorization = authorization_buffer.c_str();
        break;

    default:
        daemon_log(4, "unknown translation packet: %u\n", unsigned(cmd));
        break;
    }
}
