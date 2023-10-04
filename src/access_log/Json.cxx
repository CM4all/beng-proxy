// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * An access logger which emits JSON.
 */

#include "JsonWriter.hxx"
#include "Server.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "net/FormatAddress.hxx"
#include "net/log/String.hxx"
#include "time/Cast.hxx"
#include "time/ISO8601.hxx"
#include "util/StringBuffer.hxx"

#include <functional>

static void
Dump(JsonWriter::Sink sink, const ReceivedAccessLogDatagram &d)
{
	JsonWriter::Object o(sink);

	if (!d.logger_client_address.IsNull() &&
	    d.logger_client_address.IsDefined()) {
		char buffer[1024];
		if (ToString(buffer, d.logger_client_address))
			o.AddMember("logger_client", buffer);
	}

	if (d.HasTimestamp()) {
		try {
			o.AddMember("time", FormatISO8601(d.timestamp).c_str());
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

	if (d.message.data() != nullptr)
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
		o.AddMember("duration", ToFloatSeconds(d.duration));

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
