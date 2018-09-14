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

/*
 * An access logger which emits JSON.
 */

#include "JsonWriter.hxx"
#include "Server.hxx"
#include "net/ToString.hxx"
#include "net/log/String.hxx"
#include "time/Convert.hxx"

#include <functional>

#include <time.h>

static void
Dump(JsonWriter::Sink sink, const ReceivedAccessLogDatagram &d)
{
    JsonWriter::Object o(sink);

    if (!d.logger_client_address.IsNull() &&
        d.logger_client_address.IsDefined()) {
        char buffer[1024];
        if (ToString(buffer, sizeof(buffer), d.logger_client_address))
            o.AddMember("logger_client", buffer);
    }

    if (d.HasTimestamp()) {
        try {
            const auto tm = GmTime(Net::Log::ToSystem(d.timestamp));
            char buffer[64];
            strftime(buffer, sizeof(buffer), "%FT%TZ", &tm);
            o.AddMember("time", buffer);
        } catch (...) {
            /* just in case GmTime() throws */
        }
    }

    if (d.remote_host != nullptr)
        o.AddMember("remote_host", d.remote_host);

    if (d.host != nullptr)
        o.AddMember("host", d.host);

    if (d.site != nullptr)
        o.AddMember("site", d.site);

    if (d.forwarded_to != nullptr)
        o.AddMember("forwarded_to", d.forwarded_to);

    if (d.HasHttpMethod() &&
        http_method_is_valid(d.http_method))
        o.AddMember("method", http_method_to_string(d.http_method));

    if (d.http_uri != nullptr)
        o.AddMember("uri", d.http_uri);

    if (d.http_referer != nullptr)
        o.AddMember("referer", d.http_referer);

    if (d.user_agent != nullptr)
        o.AddMember("user_agent", d.user_agent);

    if (d.message != nullptr)
        o.AddMember("message", d.message);

    if (d.HasHttpStatus())
        o.AddMember("status", http_status_to_string(d.http_status));

    if (d.valid_length)
        o.AddMember("length", d.length);

    if (d.valid_traffic) {
        o.AddMember("traffic_received", d.traffic_received);
        o.AddMember("traffic_sent", d.traffic_sent);
    }

    if (d.valid_duration)
        o.AddMember("duration", std::chrono::duration_cast<std::chrono::duration<double>>(d.duration).count());

    if (d.type != Net::Log::Type::UNSPECIFIED) {
        const char *type = ToString(d.type);
        if (type != nullptr)
            o.AddMember("type", type);
    }

    o.Flush();

    sink.NewLine();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    AccessLogServer().Run(std::bind(Dump, JsonWriter::Sink(stdout),
                                    std::placeholders::_1));
    return 0;
}
