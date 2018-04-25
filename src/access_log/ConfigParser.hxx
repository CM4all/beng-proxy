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

#ifndef ACCESS_LOG_CONFIG_PARSER_HXX
#define ACCESS_LOG_CONFIG_PARSER_HXX

#include "Config.hxx"
#include "io/ConfigParser.hxx"

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
class AccessLogConfigParser : public ConfigParser {
    AccessLogConfig config;
    bool enabled = true, type_selected = false;

    const bool is_child_error_logger;

public:
    explicit AccessLogConfigParser(bool _is_child_error_logger=false) noexcept
        :is_child_error_logger(_is_child_error_logger) {}

    bool IsChildErrorLogger() const noexcept {
        return is_child_error_logger;
    }

    AccessLogConfig &&GetConfig() {
        return std::move(config);
    }

protected:
    /* virtual methods from class ConfigParser */
    void ParseLine(FileLineParser &line) override;
    void Finish() override;
};

#endif
