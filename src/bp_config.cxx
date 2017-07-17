/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_config.hxx"
#include "avahi/Check.hxx"
#include "spawn/ConfigParser.hxx"
#include "io/FileLineParser.hxx"
#include "io/ConfigParser.hxx"
#include "net/Parser.hxx"
#include "util/StringView.hxx"
#include "util/StringParser.hxx"

#include <string.h>

void
BpConfig::HandleSet(StringView name, const char *value)
{
    if (name.Equals("max_connections")) {
        max_connections = ParsePositiveLong(value, 1024 * 1024);
    } else if (name.Equals("tcp_stock_limit")) {
        tcp_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("fastcgi_stock_limit")) {
        fcgi_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("fcgi_stock_max_idle")) {
        fcgi_stock_max_idle = ParseUnsignedLong(value);
    } else if (name.Equals("was_stock_limit")) {
        was_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("was_stock_max_idle")) {
        was_stock_max_idle = ParseUnsignedLong(value);
    } else if (name.Equals("http_cache_size")) {
        http_cache_size = ParseSize(value);
        http_cache_size_set = true;
    } else if (name.Equals("filter_cache_size")) {
        filter_cache_size = ParseSize(value);
    } else if (name.Equals("nfs_cache_size")) {
        nfs_cache_size = ParseSize(value);
    } else if (name.Equals("translate_cache_size")) {
        translate_cache_size = ParseUnsignedLong(value);
    } else if (name.Equals("translate_stock_limit")) {
        translate_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("stopwatch")) {
        stopwatch = ParseBool(value);
    } else if (name.Equals("dump_widget_tree")) {
        dump_widget_tree = ParseBool(value);
    } else if (name.Equals("verbose_response")) {
        verbose_response = ParseBool(value);
    } else if (name.Equals("session_cookie")) {
        if (*value == 0)
            throw std::runtime_error("Invalid value");

        session_cookie = value;
    } else if (name.Equals("dynamic_session_cookie")) {
        dynamic_session_cookie = ParseBool(value);
    } else if (name.Equals("session_idle_timeout")) {
        session_idle_timeout = ParsePositiveDuration(value);
    } else if (name.Equals("session_save_path")) {
        session_save_path = value;
    } else
        throw std::runtime_error("Unknown variable");
}

class BpConfigParser final : public NestedConfigParser {
    BpConfig &config;

    class Listener final : public ConfigParser {
        BpConfigParser &parent;
        BpConfig::Listener config;

    public:
        explicit Listener(BpConfigParser &_parent):parent(_parent) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

    class Control final : public ConfigParser {
        BpConfigParser &parent;
        BpConfig::ControlListener config;

    public:
        explicit Control(BpConfigParser &_parent)
            :parent(_parent) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

public:
    explicit BpConfigParser(BpConfig &_config)
        :config(_config) {}

protected:
    /* virtual methods from class NestedConfigParser */
    void ParseLine2(FileLineParser &line) override;

private:
    void CreateListener(FileLineParser &line);
    void CreateControl(FileLineParser &line);
};

void
BpConfigParser::Listener::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        if (!config.bind_address.IsNull())
            throw LineParser::Error("Bind address already specified");

        config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
                                                 80, true);
    } else if (strcmp(word, "interface") == 0) {
        config.interface = line.ExpectValueAndEnd();
    } else if (strcmp(word, "tag") == 0) {
        config.tag = line.ExpectValueAndEnd();
    } else if (strcmp(word, "zeroconf_service") == 0 ||
               /* old option name: */ strcmp(word, "zeroconf_type") == 0) {
        config.zeroconf_service = MakeZeroconfServiceType(line.ExpectValueAndEnd(),
                                                          "_tcp");
    } else if (strcmp(word, "reuse_port") == 0) {
        config.reuse_port = line.NextBool();
        line.ExpectEnd();
    } else if (strcmp(word, "free_bind") == 0) {
        config.free_bind = line.NextBool();
        line.ExpectEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
BpConfigParser::Listener::Finish()
{
    if (config.bind_address.IsNull())
        throw LineParser::Error("Listener has no bind address");

    parent.config.listen.emplace_front(std::move(config));

    ConfigParser::Finish();
}

inline void
BpConfigParser::CreateListener(FileLineParser &line)
{
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Listener>(*this));
}

void
BpConfigParser::Control::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
                                                 5478, true);
    } else if (strcmp(word, "multicast_group") == 0) {
        config.multicast_group = ParseSocketAddress(line.ExpectValueAndEnd(),
                                                    0, false);
    } else
        throw LineParser::Error("Unknown option");
}

void
BpConfigParser::Control::Finish()
{
    if (config.bind_address.IsNull())
        throw LineParser::Error("Bind address is missing");

    if (config.multicast_group.IsNull() &&
        !parent.config.multicast_group.IsNull())
        /* default to the --multicast-group setting (for backwards
           compatibility) */
        config.multicast_group = parent.config.multicast_group;

    parent.config.control_listen.emplace_front(std::move(config));

    ConfigParser::Finish();
}

inline void
BpConfigParser::CreateControl(FileLineParser &line)
{
    line.ExpectSymbolAndEol('{');
    SetChild(std::make_unique<Control>(*this));
}

void
BpConfigParser::ParseLine2(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "listener") == 0)
        CreateListener(line);
    else if (strcmp(word, "control") == 0)
        CreateControl(line);
    else if (strcmp(word, "access_logger") == 0)
        config.access_log.SetLegacy(line.ExpectValueAndEnd());
    else if (strcmp(word, "set") == 0) {
        const char *name = line.ExpectWord();
        line.ExpectSymbol('=');
        const char *value = line.ExpectValueAndEnd();
        config.HandleSet(name, value);
    } else if (strcmp(word, "spawn") == 0) {
        line.ExpectSymbolAndEol('{');
        SetChild(std::make_unique<SpawnConfigParser>(config.spawn));
    } else
        throw LineParser::Error("Unknown option");
}

void
LoadConfigFile(BpConfig &config, const char *path)
{
    BpConfigParser parser(config);
    VariableConfigParser v_parser(parser);
    CommentConfigParser parser2(v_parser);
    IncludeConfigParser parser3(path, parser2);

    ParseConfigFile(path, parser3);
}
