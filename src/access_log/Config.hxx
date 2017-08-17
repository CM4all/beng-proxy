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

#ifndef ACCESS_LOG_CONFIG_HXX
#define ACCESS_LOG_CONFIG_HXX

#include "net/AllocatedSocketAddress.hxx"

#include <string>
#include <set>

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
struct AccessLogConfig {
    enum class Type {
        DISABLED,
        INTERNAL,
        SEND,
        EXECUTE,
    } type = Type::INTERNAL;

    /**
     * An address where we will send access log datagrams.
     */
    AllocatedSocketAddress send_to;

    /**
     * A command to be executed with a shell, where fd0 is a socket
     * which receives access log datagrams.
     *
     * Special value "null" specifies that access logging is disabled
     * completely, and "" (empty string) specifies that one-line
     * logging is performed directly to stdandard output.
     */
    std::string command;

    /**
     * Don't log this request URI if host=="localhost" and
     * status==200.
     */
    std::string ignore_localhost_200;

    /**
     * A list of proxy servers whose "X-Forwarded-For" header will be
     * trusted.
     */
    std::set<std::string> trust_xff;

    /**
     * Setter for the deprecated "--access-logger" command-line
     * option, which has a few special cases.
     */
    void SetLegacy(const char *new_value) {
        command = new_value;

        if (command.empty() || command == "internal")
            type = Type::INTERNAL;
        else if (command == "null")
            type = Type::DISABLED;
        else
            type = Type::EXECUTE;
    }
};

#endif
