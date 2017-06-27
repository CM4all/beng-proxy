/*
 * An access logger which emits JSON.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "Datagram.hxx"
#include "util/StringFormat.hxx"

#include <json/json.h>

#include <iostream>
#include <functional>
#include <memory>
#include <cinttypes>

#include <time.h>

static Json::Value
ToJson(const AccessLogDatagram &d)
{
    Json::Value root;

    if (d.valid_timestamp) {
        time_t t = d.timestamp / 1000000;
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%FT%TZ", gmtime(&t));
        root["time"] = buffer;
    }

    if (d.remote_host != nullptr)
        root["remote_host"] = d.remote_host;

    if (d.host != nullptr)
        root["host"] = d.host;

    if (d.site != nullptr)
        root["site"] = d.site;

    if (d.valid_http_method &&
        http_method_is_valid(d.http_method))
        root["method"] = http_method_to_string(d.http_method);

    if (d.http_uri != nullptr)
        root["uri"] = d.http_uri;

    if (d.http_referer != nullptr)
        root["referer"] = d.http_referer;

    if (d.user_agent != nullptr)
        root["user_agent"] = d.user_agent;

    if (d.valid_http_status)
        root["status"] = http_status_to_string(d.http_status);

    if (d.valid_length)
        root["length"] = StringFormat<64>("%" PRIu64, d.length).c_str();

    if (d.valid_traffic) {
        root["traffic_received"] = StringFormat<64>("%" PRIu64, d.traffic_received).c_str();
        root["traffic_sent"] = StringFormat<64>("%" PRIu64, d.traffic_sent).c_str();
    }

    if (d.valid_duration)
        root["duration"] = StringFormat<64>("%f", d.duration * 1e-6).c_str();

    return root;
}

#ifdef JSONCPP_VERSION_MAJOR

static void
Dump(Json::StreamWriter &writer, const AccessLogDatagram &d)
{
    writer.write(ToJson(d), &std::cout);
    std::cout << std::endl;
}

#else
// libjsoncpp 0.6 (Debian Jessie)

static void
Dump(Json::StyledStreamWriter &writer, const AccessLogDatagram &d)
{
    writer.write(std::cout, ToJson(d));
    std::cout << std::endl;
}

#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef JSONCPP_VERSION_MAJOR
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "   ";

    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
#else
    // libjsoncpp 0.6 (Debian Jessie)
    auto writer = std::make_unique<Json::StyledStreamWriter>();
#endif

    AccessLogServer(0).Run(std::bind(Dump, std::ref(*writer),
                                     std::placeholders::_1));
    return 0;
}
