/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_config.hxx"
#include "io/LineParser.hxx"
#include "io/ConfigParser.hxx"
#include "system/Error.hxx"
#include "net/Parser.hxx"
#include "util/Error.hxx"
#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

class LbConfigParser final : public NestedConfigParser {
    struct pool &pool;
    LbConfig &config;

    class Control final : public ConfigParser {
        LbConfigParser &parent;
        LbControlConfig config;

    public:
        explicit Control(LbConfigParser &_parent)
            :parent(_parent) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class CertDatabase final : public ConfigParser {
        LbConfigParser &parent;
        LbCertDatabaseConfig config;

    public:
        CertDatabase(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class Monitor final : public ConfigParser {
        LbConfigParser &parent;
        LbMonitorConfig config;

    public:
        Monitor(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class Node final : public ConfigParser {
        LbConfigParser &parent;
        LbNodeConfig config;

    public:
        Node(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class Cluster final : public ConfigParser {
        LbConfigParser &parent;
        LbClusterConfig config;

    public:
        Cluster(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class Branch final : public ConfigParser {
        LbConfigParser &parent;
        LbBranchConfig config;

    public:
        Branch(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

    class Listener final : public ConfigParser {
        LbConfigParser &parent;
        LbListenerConfig config;

    public:
        Listener(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(LineParser &line) override;
        void Finish() override;
    };

public:
    LbConfigParser(struct pool &_pool, LbConfig &_config)
        :pool(_pool), config(_config) {}

protected:
    /* virtual methods from class ConfigParser */
    void Finish() override;

    /* virtual methods from class NestedConfigParser */
    void ParseLine2(LineParser &line) override;

private:
    void CreateControl(LineParser &line);
    void CreateCertDatabase(LineParser &line);
    void CreateMonitor(LineParser &line);
    void CreateNode(LineParser &line);

    LbNodeConfig &AutoCreateNode(const char *name);
    void AutoCreateMember(LbMemberConfig &member, const char *name);

    void CreateCluster(LineParser &line);
    void CreateBranch(LineParser &line);
    void CreateListener(LineParser &line);
};

void
LbConfigParser::Control::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        const char *address = line.ExpectValueAndEnd();

        config.bind_address = ParseSocketAddress(address, 5478, true);
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Control::Finish()
{
    if (config.bind_address.IsNull())
        throw LineParser::Error("Bind address is missing");

    parent.config.controls.emplace_back(std::move(config));

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateControl(LineParser &line)
{
    line.ExpectSymbolAndEol('{');
    SetChild(std::make_unique<Control>(*this));
}

void
LbConfigParser::CertDatabase::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "connect") == 0) {
        config.connect = line.ExpectValueAndEnd();
    } else if (strcmp(word, "schema") == 0) {
        config.schema = line.ExpectValueAndEnd();
    } else if (strcmp(word, "ca_cert") == 0) {
        config.ca_certs.emplace_back(line.ExpectValueAndEnd());
    } else if (strcmp(word, "wrap_key") == 0) {
        const char *name = line.ExpectValue();
        const char *hex_key = line.ExpectValue();
        line.ExpectEnd();

        CertDatabaseConfig::AES256 key;
        if (strlen(hex_key) != key.size() * 2)
            throw LineParser::Error("Malformed AES256 key");

        for (unsigned i = 0; i < sizeof(key); ++i) {
            const char b[3] = { hex_key[i * 2], hex_key[i * 2 + 1], 0 };
            char *endptr;
            unsigned long v = strtoul(b, &endptr, 16);
            if (endptr != b + 2 || v >= 0xff)
                throw LineParser::Error("Malformed AES256 key");

            key[i] = v;
        }

        auto i = config.wrap_keys.emplace(name, key);
        if (!i.second)
            throw LineParser::Error("Duplicate wrap_key name");

        if (config.default_wrap_key.empty())
            config.default_wrap_key = i.first->first;
    } else
        throw std::runtime_error("Unknown option");
}

void
LbConfigParser::CertDatabase::Finish()
{
    config.Check();

    auto i = parent.config.cert_dbs.emplace(std::string(config.name),
                                            std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate certdb name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateCertDatabase(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<CertDatabase>(*this, name));
}

void
LbConfigParser::Monitor::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "type") == 0) {
        if (config.type != LbMonitorConfig::Type::NONE)
            throw LineParser::Error("Monitor type already specified");

        const char *value = line.ExpectValueAndEnd();
        if (strcmp(value, "none") == 0)
            config.type = LbMonitorConfig::Type::NONE;
        else if (strcmp(value, "ping") == 0)
            config.type = LbMonitorConfig::Type::PING;
        else if (strcmp(value, "connect") == 0)
            config.type = LbMonitorConfig::Type::CONNECT;
        else if (strcmp(value, "tcp_expect") == 0)
            config.type = LbMonitorConfig::Type::TCP_EXPECT;
        else
            throw LineParser::Error("Unknown monitor type");
    } else if (strcmp(word, "interval") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        config.interval = value;
    } else if (strcmp(word, "timeout") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        config.timeout = value;
    } else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "connect_timeout") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        config.connect_timeout = value;
    } else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "send") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        config.send = value;
    } else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        config.expect = value;
    } else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect_graceful") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        config.fade_expect = value;
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Monitor::Finish()
{
    if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
        (config.expect.empty() && config.fade_expect.empty()))
        throw LineParser::Error("No 'expect' string configured");

    auto i = parent.config.monitors.emplace(std::string(config.name),
                                            std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate monitor name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateMonitor(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Monitor>(*this, name));
}

void
LbConfigParser::Node::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "address") == 0) {
        if (!config.address.IsNull())
            throw LineParser::Error("Duplicate node address");

        const char *value = line.ExpectValueAndEnd();

        config.address = ParseSocketAddress(value, 80, false);
    } else if (strcmp(word, "jvm_route") == 0) {
        if (!config.jvm_route.empty())
            throw LineParser::Error("Duplicate jvm_route");

        config.jvm_route = line.ExpectValueAndEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Node::Finish()
{
    if (config.address.IsNull())
        config.address = ParseSocketAddress(config.name.c_str(), 80, false);

    auto i = parent.config.nodes.emplace(std::string(config.name),
                                         std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate node name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateNode(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Node>(*this, name));
}

LbNodeConfig &
LbConfigParser::AutoCreateNode(const char *name)
{
    auto address = ParseSocketAddress(name, 80, false);

    auto i = config.nodes.insert(std::make_pair(name,
                                                LbNodeConfig(name,
                                                             std::move(address))));
    return i.first->second;
}

void
LbConfigParser::AutoCreateMember(LbMemberConfig &member, const char *name)
{
    member.node = &AutoCreateNode(name);
    member.port = 0;
}

static unsigned
parse_port(const char *p, SocketAddress address)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = address.GetFamily();
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *ai;
    if (getaddrinfo(nullptr, p, &hints, &ai) != 0)
        return 0;

    unsigned port = SocketAddress(ai->ai_addr, ai->ai_addrlen).GetPort();
    freeaddrinfo(ai);
    return port;
}

gcc_pure
static bool
validate_protocol_sticky(LbProtocol protocol, StickyMode sticky)
{
    switch (protocol) {
    case LbProtocol::HTTP:
        return true;

    case LbProtocol::TCP:
        switch (sticky) {
        case StickyMode::NONE:
        case StickyMode::FAILOVER:
        case StickyMode::SOURCE_IP:
            return true;

        case StickyMode::SESSION_MODULO:
        case StickyMode::COOKIE:
        case StickyMode::JVM_ROUTE:
            return false;
        }
    }

    return false;
}

gcc_pure
static StickyMode
ParseStickyMode(const char *s)
{
    if (strcmp(s, "none") == 0)
        return StickyMode::NONE;
    else if (strcmp(s, "failover") == 0)
        return StickyMode::FAILOVER;
    else if (strcmp(s, "source_ip") == 0)
        return StickyMode::SOURCE_IP;
    else if (strcmp(s, "session_modulo") == 0)
        return StickyMode::SESSION_MODULO;
    else if (strcmp(s, "cookie") == 0)
        return StickyMode::COOKIE;
    else if (strcmp(s, "jvm_route") == 0)
        return StickyMode::JVM_ROUTE;
    else
        throw LineParser::Error("Unknown sticky mode");
}

void
LbConfigParser::Cluster::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "name") == 0) {
        config.name = line.ExpectValueAndEnd();
    } else if (strcmp(word, "sticky") == 0) {
        config.sticky_mode = ParseStickyMode(line.ExpectValueAndEnd());
    } else if (strcmp(word, "session_cookie") == 0) {
        config.session_cookie = line.ExpectValueAndEnd();
    } else if (strcmp(word, "monitor") == 0) {
        if (config.monitor != nullptr)
            throw LineParser::Error("Monitor already specified");

        config.monitor = parent.config.FindMonitor(line.ExpectValueAndEnd());
        if (config.monitor == nullptr)
            throw LineParser::Error("No such monitor");
    } else if (strcmp(word, "member") == 0) {
        if (!config.zeroconf_service.empty() ||
            !config.zeroconf_domain.empty())
            throw LineParser::Error("Cannot configure both hard-coded members and Zeroconf");

        char *name = line.ExpectValue();

        /*
        line.ExpectEnd();
        */

        config.members.emplace_back();

        auto *member = &config.members.back();

        member->node = parent.config.FindNode(name);
        if (member->node == nullptr) {
            char *q = strchr(name, ':');
            if (q != nullptr) {
                *q++ = 0;
                member->node = parent.config.FindNode(name);
                if (member->node == nullptr) {
                    /* node doesn't exist: parse the given member
                       name, auto-create a new node */

                    /* restore the colon */
                    *--q = ':';

                    parent.AutoCreateMember(*member, name);
                    return;
                }

                member->port = parse_port(q, member->node->address);
                if (member->port == 0)
                    throw LineParser::Error("Malformed port");
            } else
                /* node doesn't exist: parse the given member
                   name, auto-create a new node */
                parent.AutoCreateMember(*member, name);
        }
    } else if (strcmp(word, "zeroconf_service") == 0) {
        if (!config.members.empty())
            throw LineParser::Error("Cannot configure both hard-coded members and Zeroconf");

        if (!config.zeroconf_service.empty())
            throw LineParser::Error("Duplicate zeroconf_service");

        config.zeroconf_service = line.ExpectValueAndEnd();
    } else if (strcmp(word, "zeroconf_domain") == 0) {
        if (!config.members.empty())
            throw LineParser::Error("Cannot configure both hard-coded members and Zeroconf");

        if (!config.zeroconf_domain.empty())
            throw LineParser::Error("Duplicate zeroconf_domain");

        config.zeroconf_domain = line.ExpectValueAndEnd();
    } else if (strcmp(word, "protocol") == 0) {
        const char *protocol = line.ExpectValueAndEnd();
        if (strcmp(protocol, "http") == 0)
            config.protocol = LbProtocol::HTTP;
        else if (strcmp(protocol, "tcp") == 0)
            config.protocol = LbProtocol::TCP;
        else
            throw LineParser::Error("Unknown protocol");
    } else if (strcmp(word, "source_address") == 0) {
        const char *address = line.ExpectValueAndEnd();
        if (strcmp(address, "transparent") != 0)
            throw LineParser::Error("\"transparent\" expected");

        config.transparent_source = true;
    } else if (strcmp(word, "mangle_via") == 0) {
        config.mangle_via = line.NextBool();

        line.ExpectEnd();
    } else if (strcmp(word, "fallback") == 0) {
        if (config.fallback.IsDefined())
            throw LineParser::Error("Duplicate fallback");

        const char *location = line.ExpectValue();
        if (strstr(location, "://") != nullptr) {
            line.ExpectEnd();

            config.fallback.location = location;
        } else {
            char *endptr;
            http_status_t status =
                (http_status_t)(unsigned)strtoul(location, &endptr, 10);
            if (*endptr != 0 || !http_status_is_valid(status))
                throw LineParser::Error("Invalid HTTP status code");

            if (http_status_is_empty(status))
                throw LineParser::Error("This HTTP status does not allow a response body");

            const char *message = line.ExpectValue();

            line.ExpectEnd();

            config.fallback.status = status;
            config.fallback.message = message;
        }
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Cluster::Finish()
{
    if (!config.zeroconf_domain.empty() &&
        config.zeroconf_service.empty())
        throw LineParser::Error("zeroconf_service missing");

    if (config.members.empty() && config.zeroconf_service.empty())
        throw LineParser::Error("Pool has no members");

    if (!validate_protocol_sticky(config.protocol, config.sticky_mode))
        throw LineParser::Error("Sticky mode not available for this protocol");

    if (config.members.size() == 1)
        /* with only one member, a sticky setting doesn't make
           sense */
        config.sticky_mode = StickyMode::NONE;

    auto i = parent.config.clusters.emplace(std::string(config.name),
                                            std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate pool name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateCluster(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Cluster>(*this, name));
}

static bool
parse_attribute_reference(LbAttributeReference &a, const char *p)
{
    if (strcmp(p, "request_method") == 0) {
        a.type = LbAttributeReference::Type::METHOD;
        return true;
    } else if (strcmp(p, "request_uri") == 0) {
        a.type = LbAttributeReference::Type::URI;
        return true;
    } else if (memcmp(p, "http_", 5) == 0) {
        a.type = LbAttributeReference::Type::HEADER;
        a.name = p + 5;
        if (a.name.empty())
            return false;

        for (auto &ch : a.name) {
            if (ch == '_')
                ch = '-';
            else if (!g_ascii_islower(ch) && !g_ascii_isdigit(ch))
                return false;
        }

        return true;
    } else
        return false;
}

void
LbConfigParser::Branch::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "goto") == 0) {
        const char *name = line.ExpectValue();

        LbGoto destination = parent.config.FindGoto(name);
        if (!destination.IsDefined())
            throw LineParser::Error("No such pool");

        if (line.IsEnd()) {
            if (config.HasFallback())
                throw LineParser::Error("Fallback already specified");

            if (!config.conditions.empty() &&
                config.conditions.front().destination.GetProtocol() != destination.GetProtocol())
                throw LineParser::Error("Protocol mismatch");

            config.fallback = destination;
            return;
        }

        if (config.fallback.IsDefined() &&
            config.fallback.GetProtocol() != destination.GetProtocol())
                throw LineParser::Error("Protocol mismatch");

        const char *if_ = line.NextWord();
        if (if_ == nullptr || strcmp(if_, "if") != 0)
            throw LineParser::Error("'if' or end of line expected");

        if (!line.SkipSymbol('$'))
            throw LineParser::Error("Attribute name starting with '$' expected");

        const char *attribute = line.NextWord();
        if (attribute == nullptr)
            throw LineParser::Error("Attribute name starting with '$' expected");

        LbConditionConfig::Operator op;
        bool negate;

        if (line.SkipSymbol('=', '=')) {
            op = LbConditionConfig::Operator::EQUALS;
            negate = false;
        } else if (line.SkipSymbol('!', '=')) {
            op = LbConditionConfig::Operator::EQUALS;
            negate = true;
        } else if (line.SkipSymbol('=', '~')) {
            op = LbConditionConfig::Operator::REGEX;
            negate = false;
        } else if (line.SkipSymbol('!', '~')) {
            op = LbConditionConfig::Operator::REGEX;
            negate = true;
        } else
            throw LineParser::Error("Comparison operator expected");

        line.ExpectWhitespace();

        const char *string = line.NextUnescape();
        if (string == nullptr)
            throw LineParser::Error("Regular expression expected");

        line.ExpectEnd();

        LbAttributeReference a(LbAttributeReference::Type::HEADER, "");
        if (!parse_attribute_reference(a, attribute))
            throw LineParser::Error("Unknown attribute reference");

        UniqueRegex regex;
        if (op == LbConditionConfig::Operator::REGEX) {
            Error error;
            if (!regex.Compile(string, false, false, error))
                throw LineParser::Error(error.GetMessage());
        }

        LbGotoIfConfig gif(regex.IsDefined()
                           ? LbConditionConfig(std::move(a), negate,
                                               std::move(regex))
                           : LbConditionConfig(std::move(a), negate,
                                               string),
                           destination);
        config.conditions.emplace_back(std::move(gif));
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Branch::Finish()
{
    if (!config.HasFallback())
        throw LineParser::Error("Branch has no fallback");

    if (config.GetProtocol() != LbProtocol::HTTP)
        throw LineParser::Error("Only HTTP pools allowed in branch");

    auto i = parent.config.branches.emplace(std::string(config.name),
                                            std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate pool/branch name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateBranch(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Branch>(*this, name));
}

void
LbConfigParser::Listener::ParseLine(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        const char *address = line.ExpectValueAndEnd();

        config.bind_address = ParseSocketAddress(address, 80, true);
    } else if (strcmp(word, "pool") == 0) {
        if (config.destination.IsDefined())
            throw LineParser::Error("Pool already configured");

        config.destination = parent.config.FindGoto(line.ExpectValueAndEnd());
        if (!config.destination.IsDefined())
            throw LineParser::Error("No such pool");
    } else if (strcmp(word, "verbose_response") == 0) {
        bool value = line.NextBool();

        line.ExpectEnd();

        config.verbose_response = value;
    } else if (strcmp(word, "ssl") == 0) {
        bool value = line.NextBool();

        if (config.ssl && !value)
            throw LineParser::Error("SSL cannot be disabled at this point");

        line.ExpectEnd();

        config.ssl = value;
    } else if (strcmp(word, "ssl_cert_db") == 0) {
        if (!config.ssl)
            throw LineParser::Error("SSL is not enabled");

        if (config.cert_db != nullptr)
            throw LineParser::Error("ssl_cert_db already set");

        const char *name = line.ExpectValueAndEnd();
        config.cert_db = parent.config.FindCertDb(name);
        if (config.cert_db == nullptr)
            throw LineParser::Error(std::string("No such cert_db: ") + name);
    } else if (strcmp(word, "ssl_cert") == 0) {
        if (!config.ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *path = line.ExpectValue();

        const char *key_path = nullptr;
        if (!line.IsEnd())
            key_path = line.ExpectValue();

        line.ExpectEnd();

        auto &cks = config.ssl_config.cert_key;
        if (!cks.empty()) {
            auto &front = cks.front();

            if (key_path == nullptr) {
                if (front.cert_file.empty()) {
                    front.cert_file = path;
                    return;
                } else
                    throw LineParser::Error("Certificate already configured");
            } else {
                if (front.cert_file.empty())
                    throw LineParser::Error("Previous certificate missing");
                if (front.key_file.empty())
                    throw LineParser::Error("Previous key missing");
            }
        }

        if (key_path == nullptr)
            key_path = "";

        cks.emplace_back(path, key_path);
    } else if (strcmp(word, "ssl_key") == 0) {
        if (!config.ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *path = line.ExpectValueAndEnd();

        auto &cks = config.ssl_config.cert_key;
        if (!cks.empty()) {
            if (!cks.front().key_file.empty())
                throw LineParser::Error("Key already configured");

            cks.front().key_file = path;
        } else {
            cks.emplace_back(std::string(), path);
        }
    } else if (strcmp(word, "ssl_ca_cert") == 0) {
        if (!config.ssl)
            throw LineParser::Error("SSL is not enabled");

        if (!config.ssl_config.ca_cert_file.empty())
            throw LineParser::Error("Certificate already configured");

        config.ssl_config.ca_cert_file = line.ExpectValueAndEnd();
    } else if (strcmp(word, "ssl_verify") == 0) {
        if (!config.ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *value = line.ExpectValueAndEnd();
        if (strcmp(value, "yes") == 0)
            config.ssl_config.verify = SslVerify::YES;
        else if (strcmp(value, "no") == 0)
            config.ssl_config.verify = SslVerify::NO;
        else if (strcmp(value, "optional") == 0)
            config.ssl_config.verify = SslVerify::OPTIONAL;
        else
            throw LineParser::Error("yes/no expected");
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Listener::Finish()
{
    if (parent.config.FindListener(config.name) != nullptr)
        throw LineParser::Error("Duplicate listener name");

    if (config.bind_address.IsNull())
        throw LineParser::Error("Listener has no destination");

    if (config.ssl &&
        !config.ssl_config.IsValid(config.cert_db != nullptr))
        throw LineParser::Error("Incomplete SSL configuration");

    parent.config.listeners.emplace_back(std::move(config));

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateListener(LineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Listener>(*this, name));
}

void
LbConfigParser::ParseLine2(LineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "node") == 0)
        CreateNode(line);
    else if (strcmp(word, "pool") == 0)
        CreateCluster(line);
    else if (strcmp(word, "branch") == 0)
        CreateBranch(line);
    else if (strcmp(word, "listener") == 0)
        CreateListener(line);
    else if (strcmp(word, "monitor") == 0)
        CreateMonitor(line);
    else if (strcmp(word, "cert_db") == 0)
        CreateCertDatabase(line);
    else if (strcmp(word, "control") == 0)
        CreateControl(line);
    else
        throw LineParser::Error("Unknown option");
}

static void
lb_cluster_config_finish(struct pool *pool, LbClusterConfig &config)
{
    config.address_list.SetStickyMode(config.sticky_mode);

    for (auto &member : config.members) {
        AllocatedSocketAddress address = member.node->address;
        if (member.port != 0)
            address.SetPort(member.port);

        if (!config.address_list.Add(pool, address))
            throw LineParser::Error("Too many members");
    }
}

void
LbConfigParser::Finish()
{
    for (auto &i : config.clusters)
        lb_cluster_config_finish(&pool, i.second);

    NestedConfigParser::Finish();
}

LbConfig
lb_config_load(struct pool *pool, const char *path)
{
    LbConfig config;
    LbConfigParser parser(*pool, config);
    CommentConfigParser parser2(parser);
    IncludeConfigParser parser3(path, parser2);

    ParseConfigFile(path, parser3);
    return config;
}

int
LbClusterConfig::FindJVMRoute(const char *jvm_route) const
{
    assert(jvm_route != nullptr);

    for (unsigned i = 0, n = members.size(); i < n; ++i) {
        const auto &node = *members[i].node;

        if (!node.jvm_route.empty() && node.jvm_route == jvm_route)
            return i;
    }

    return -1;
}
