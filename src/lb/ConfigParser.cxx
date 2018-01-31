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

#include "Config.hxx"
#include "Check.hxx"
#include "AllocatorPtr.hxx"
#include "access_log/ConfigParser.hxx"
#include "avahi/Check.hxx"
#include "io/FileLineParser.hxx"
#include "io/ConfigParser.hxx"
#include "system/Error.hxx"
#include "net/Parser.hxx"
#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"
#include "util/RuntimeError.hxx"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

class LbConfigParser final : public NestedConfigParser {
    LbConfig &config;

    class Control final : public ConfigParser {
        LbConfigParser &parent;
        LbControlConfig config;

    public:
        explicit Control(LbConfigParser &_parent)
            :parent(_parent) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
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
        void ParseLine(FileLineParser &line) override;
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
        void ParseLine(FileLineParser &line) override;
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
        void ParseLine(FileLineParser &line) override;
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
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

    class Branch final : public ConfigParser {
        LbConfigParser &parent;
        LbBranchConfig config;

    public:
        Branch(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    private:
        void AddGoto(LbGotoConfig &&destination, FileLineParser &line);

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

    class LuaHandler final : public ConfigParser {
        LbConfigParser &parent;
        LbLuaHandlerConfig config;

    public:
        LuaHandler(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

    class TranslationHandler final : public ConfigParser {
        LbConfigParser &parent;
        LbTranslationHandlerConfig config;

    public:
        TranslationHandler(LbConfigParser &_parent, const char *_name)
            :parent(_parent), config(_name) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
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
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

    class GlobalHttpCheck final : public ConfigParser {
        LbConfigParser &parent;
        LbHttpCheckConfig config;

    public:
        explicit GlobalHttpCheck(LbConfigParser &_parent)
            :parent(_parent) {}

    protected:
        /* virtual methods from class ConfigParser */
        void ParseLine(FileLineParser &line) override;
        void Finish() override;
    };

public:
    explicit LbConfigParser(LbConfig &_config)
        :config(_config) {}

protected:
    /* virtual methods from class NestedConfigParser */
    void ParseLine2(FileLineParser &line) override;
    void FinishChild(std::unique_ptr<ConfigParser> &&child) override;

private:
    void CreateControl(FileLineParser &line);
    void CreateCertDatabase(FileLineParser &line);
    void CreateMonitor(FileLineParser &line);
    void CreateNode(FileLineParser &line);

    LbNodeConfig &AutoCreateNode(const char *name);
    void AutoCreateMember(LbMemberConfig &member, const char *name);

    void CreateCluster(FileLineParser &line);
    void CreateBranch(FileLineParser &line);
    void CreateLuaHandler(FileLineParser &line);
    void CreateTranslationHandler(FileLineParser &line);
    void CreateListener(FileLineParser &line);
    void CreateGlobalHttpCheck(FileLineParser &line);
};

void
LbConfigParser::Control::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        const char *address = line.ExpectValueAndEnd();

        config.bind_address = ParseSocketAddress(address, 5478, true);
    } else if (strcmp(word, "multicast_group") == 0) {
        config.multicast_group = ParseSocketAddress(line.ExpectValueAndEnd(),
                                                    0, false);
    } else if (strcmp(word, "interface") == 0) {
        config.interface = line.ExpectValueAndEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Control::Finish()
{
    if (config.bind_address.IsNull())
        throw LineParser::Error("Bind address is missing");

    config.Fixup();

    parent.config.controls.emplace_back(std::move(config));

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateControl(FileLineParser &line)
{
    line.ExpectSymbolAndEol('{');
    SetChild(std::make_unique<Control>(*this));
}

inline void
LbConfigParser::CreateGlobalHttpCheck(FileLineParser &line)
{
    line.ExpectSymbolAndEol('{');

    if (config.global_http_check)
        throw LineParser::Error("'global_http_check' already configured");

    SetChild(std::make_unique<GlobalHttpCheck>(*this));
}

void
LbConfigParser::CertDatabase::ParseLine(FileLineParser &line)
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
LbConfigParser::CreateCertDatabase(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<CertDatabase>(*this, name));
}

void
LbConfigParser::Monitor::ParseLine(FileLineParser &line)
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
        config.interval = line.NextPositiveInteger();
    } else if (strcmp(word, "timeout") == 0) {
        config.timeout = line.NextPositiveInteger();
    } else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "connect_timeout") == 0) {
        config.connect_timeout = line.NextPositiveInteger();
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
LbConfigParser::CreateMonitor(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Monitor>(*this, name));
}

void
LbConfigParser::Node::ParseLine(FileLineParser &line)
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
LbConfigParser::CreateNode(FileLineParser &line)
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

        case StickyMode::HOST:
        case StickyMode::XHOST:
        case StickyMode::SESSION_MODULO:
        case StickyMode::COOKIE:
        case StickyMode::JVM_ROUTE:
            return false;
        }
    }

    return false;
}

gcc_const
static bool
ValidateZeroconfSticky(StickyMode sticky) noexcept
{
    switch (sticky) {
    case StickyMode::NONE:
    case StickyMode::FAILOVER:
    case StickyMode::SOURCE_IP:
    case StickyMode::HOST:
    case StickyMode::XHOST:
        return true;

    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        return false;
    }

    gcc_unreachable();
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
    else if (strcmp(s, "host") == 0)
        return StickyMode::HOST;
    else if (strcmp(s, "xhost") == 0)
        return StickyMode::XHOST;
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
LbConfigParser::Cluster::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "name") == 0) {
        config.name = line.ExpectValueAndEnd();
    } else if (strcmp(word, "sticky") == 0) {
        config.sticky_mode = ParseStickyMode(line.ExpectValueAndEnd());
    } else if (strcmp(word, "sticky_cache") == 0) {
        config.sticky_cache = line.NextBool();
        line.ExpectEnd();
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

        config.zeroconf_service = MakeZeroconfServiceType(line.ExpectValueAndEnd(),
                                                          "_tcp");
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

            config.fallback.status = HTTP_STATUS_FOUND;
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
    if (config.monitor != nullptr && !config.monitor->IsDefined())
        /* if the monitor is disabled, it's pointless to keep a
           pointer to it */
        config.monitor = nullptr;

    if (!config.zeroconf_domain.empty() &&
        config.zeroconf_service.empty())
        throw LineParser::Error("zeroconf_service missing");

    if (config.members.empty() && !config.HasZeroConf())
        throw LineParser::Error("Pool has no members");

    if (!validate_protocol_sticky(config.protocol, config.sticky_mode))
        throw LineParser::Error("The selected sticky mode not available for this protocol");

    if (config.HasZeroConf() &&
        !ValidateZeroconfSticky(config.sticky_mode))
        throw LineParser::Error("The selected sticky mode not compatible with Zeroconf");

    if (config.members.size() == 1)
        /* with only one member, a sticky setting doesn't make
           sense */
        config.sticky_mode = StickyMode::NONE;

    auto i = parent.config.clusters.emplace(std::string(config.name),
                                            std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate pool name");

    i.first->second.FillAddressList();

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateCluster(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Cluster>(*this, name));
}

static LbAttributeReference
ParseAttributeReference(const char *p)
{
    if (strcmp(p, "request_method") == 0) {
        return LbAttributeReference::Type::METHOD;
    } else if (strcmp(p, "request_uri") == 0) {
        return LbAttributeReference::Type::URI;
    } else if (memcmp(p, "http_", 5) == 0) {
        LbAttributeReference a(LbAttributeReference::Type::HEADER, p + 5);
        if (a.name.empty())
            throw LineParser::Error("Empty HTTP header name");

        for (auto &ch : a.name) {
            if (ch == '_')
                ch = '-';
            else if (!IsLowerAlphaASCII(ch) && !IsDigitASCII(ch))
                throw LineParser::Error("Malformed HTTP header name");
        }

        return a;
    } else
        throw LineParser::Error("Unknown attribute reference");
}

static LbConditionConfig
ParseCondition(FileLineParser &line)
{
    if (!line.SkipSymbol('$'))
        throw LineParser::Error("Attribute name starting with '$' expected");

    const char *attribute = line.NextWord();
    if (attribute == nullptr)
        throw LineParser::Error("Attribute name starting with '$' expected");

    auto a = ParseAttributeReference(attribute);

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

    switch (op) {
    case LbConditionConfig::Operator::EQUALS:
        return {std::move(a), negate, string};

    case LbConditionConfig::Operator::REGEX:
        return {std::move(a), negate, UniqueRegex(string, false, false)};
    }

    gcc_unreachable();
}

static http_status_t
ParseStatus(const char *s)
{
    char *endptr;
    auto l = strtoul(s, &endptr, 10);
    if (endptr == s || *endptr != 0)
        throw LineParser::Error("Failed to parse status number");

    auto status = http_status_t(l);
    if (l < 200 || l >= 600 || !http_status_is_valid(status))
        throw LineParser::Error("Invalid status");

    return status;
}

void
LbConfigParser::Branch::AddGoto(LbGotoConfig &&destination,
                                FileLineParser &line)
{
    if (line.IsEnd()) {
        if (config.HasFallback())
            throw LineParser::Error("Fallback already specified");

        if (!config.conditions.empty() &&
            config.conditions.front().destination.GetProtocol() != destination.GetProtocol())
            throw LineParser::Error("Protocol mismatch");

        config.fallback = std::move(destination);
    } else {
        if (config.fallback.IsDefined() &&
            config.fallback.GetProtocol() != destination.GetProtocol())
            throw LineParser::Error("Protocol mismatch");

        const char *if_ = line.NextWord();
        if (if_ == nullptr || strcmp(if_, "if") != 0)
            throw LineParser::Error("'if' or end of line expected");

        auto condition = ParseCondition(line);

        line.ExpectEnd();

        config.conditions.emplace_back(std::move(condition),
                                       std::move(destination));
    }
}

void
LbConfigParser::Branch::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "goto") == 0) {
        const char *name = line.ExpectValue();

        LbGotoConfig destination = parent.config.FindGoto(name);
        if (!destination.IsDefined())
            throw LineParser::Error("No such pool");

        AddGoto(std::move(destination), line);
    } else if (strcmp(word, "status") == 0) {
        const auto status = ParseStatus(line.ExpectValue());
        LbGotoConfig destination(status);

        AddGoto(std::move(destination), line);
    } else if (strcmp(word, "redirect") == 0) {
        LbGotoConfig destination(HTTP_STATUS_FOUND);
        destination.response.location = line.ExpectValue();

        AddGoto(std::move(destination), line);
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
LbConfigParser::CreateBranch(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Branch>(*this, name));
}

void
LbConfigParser::LuaHandler::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "path") == 0) {
        if (!config.path.empty())
            throw LineParser::Error("Duplicate 'path'");

        config.path = line.ExpectPathAndEnd();
    } else if (strcmp(word, "function") == 0) {
        if (!config.function.empty())
            throw LineParser::Error("Duplicate 'function'");

        config.function = line.ExpectValueAndEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::LuaHandler::Finish()
{
    if (config.path.empty())
        throw LineParser::Error("lua_handler has no 'path'");

    if (config.function.empty())
        throw LineParser::Error("lua_handler has no 'function'");

    auto i = parent.config.lua_handlers.emplace(std::string(config.name),
                                                std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate pool/branch name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateLuaHandler(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<LuaHandler>(*this, name));
}

void
LbConfigParser::TranslationHandler::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "connect") == 0) {
        if (!config.address.IsNull())
            throw LineParser::Error("Duplicate 'connect'");

        config.address.SetLocal(line.ExpectValueAndEnd());
    } else if (strcmp(word, "pools") == 0) {
        while (!line.IsEnd()) {
            const char *name = line.ExpectValue();
            const auto destination = parent.config.FindGoto(name);
            if (!destination.IsDefined())
                throw FormatRuntimeError("No such pool: %s", name);

            if (destination.GetProtocol() != LbProtocol::HTTP)
                throw LineParser::Error("Only HTTP pools allowed");

            assert(destination.GetName() != nullptr);
            assert(strcmp(destination.GetName(), name) == 0);

            auto i = config.destinations.emplace(destination.GetName(),
                                                 std::move(destination));
            if (!i.second)
                throw FormatRuntimeError("Duplicate pool: %s", name);
        }
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::TranslationHandler::Finish()
{
    if (config.address.IsNull())
        throw LineParser::Error("translation_handler has no 'connect'");

    if (config.destinations.empty())
        throw LineParser::Error("translation_handler has no pools");

    auto i = parent.config.translation_handlers.emplace(std::string(config.name),
                                                        std::move(config));
    if (!i.second)
        throw LineParser::Error("Duplicate translation_handler name");

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateTranslationHandler(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<TranslationHandler>(*this, name));
}

void
LbConfigParser::Listener::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "bind") == 0) {
        const char *address = line.ExpectValueAndEnd();

        config.bind_address = ParseSocketAddress(address, 80, true);
    } else if (strcmp(word, "interface") == 0) {
        config.interface = line.ExpectValueAndEnd();
    } else if (strcmp(word, "tag") == 0) {
        config.tag = line.ExpectValueAndEnd();
    } else if (strcmp(word, "reuse_port") == 0) {
        config.reuse_port = line.NextBool();
        line.ExpectEnd();
    } else if (strcmp(word, "free_bind") == 0) {
        config.free_bind = line.NextBool();
        line.ExpectEnd();
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

    if (config.ssl && config.ssl_config.cert_key.empty())
        throw LineParser::Error("No SSL certificates ");

    if (config.destination.GetProtocol() == LbProtocol::HTTP ||
        config.ssl)
        config.tcp_defer_accept = 10;

    parent.config.listeners.emplace_back(std::move(config));

    ConfigParser::Finish();
}

void
LbConfigParser::GlobalHttpCheck::ParseLine(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "uri") == 0) {
        if (!config.uri.empty())
            throw LineParser::Error("'uri' already specified");

        const char *value = line.ExpectValueAndEnd();
        if (*value != '/')
            throw LineParser::Error("'uri' must be an absolute URI path");

        config.uri = value;
    } else if (strcmp(word, "host") == 0) {
        if (!config.host.empty())
            throw LineParser::Error("'host' already specified");

        const char *value = line.ExpectValueAndEnd();
        if (*value == 0)
            throw LineParser::Error("'host' must not be empty");

        config.host = value;
    } else if (strcmp(word, "client") == 0) {
        const char *value = line.ExpectValueAndEnd();
        if (*value == 0)
            throw LineParser::Error("'client' must not be empty");

        config.client_addresses.emplace_front(value);
    } else if (strcmp(word, "file_exists") == 0) {
        if (!config.file_exists.empty())
            throw LineParser::Error("'file_exists' already specified");

        const char *value = line.ExpectValueAndEnd();
        if (*value != '/')
            throw LineParser::Error("'file_exists' must be an absolute path");

        config.file_exists = value;
    } else if (strcmp(word, "success_message") == 0) {
        if (!config.success_message.empty())
            throw LineParser::Error("'success_message' already specified");

        config.success_message = line.ExpectValueAndEnd();
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::GlobalHttpCheck::Finish()
{
    if (config.uri.empty())
        throw LineParser::Error("Missing 'uri'");

    if (config.host.empty())
        throw LineParser::Error("Missing 'host'");

    if (config.file_exists.empty())
        throw LineParser::Error("Missing 'file_exists'");

    parent.config.global_http_check = std::make_unique<LbHttpCheckConfig>(std::move(config));

    ConfigParser::Finish();
}

inline void
LbConfigParser::CreateListener(FileLineParser &line)
{
    const char *name = line.ExpectValue();
    line.ExpectSymbolAndEol('{');

    SetChild(std::make_unique<Listener>(*this, name));
}

void
LbConfigParser::ParseLine2(FileLineParser &line)
{
    const char *word = line.ExpectWord();

    if (strcmp(word, "node") == 0)
        CreateNode(line);
    else if (strcmp(word, "pool") == 0)
        CreateCluster(line);
    else if (strcmp(word, "branch") == 0)
        CreateBranch(line);
    else if (strcmp(word, "lua_handler") == 0)
        CreateLuaHandler(line);
    else if (strcmp(word, "translation_handler") == 0)
        CreateTranslationHandler(line);
    else if (strcmp(word, "listener") == 0)
        CreateListener(line);
    else if (strcmp(word, "monitor") == 0)
        CreateMonitor(line);
    else if (strcmp(word, "cert_db") == 0)
        CreateCertDatabase(line);
    else if (strcmp(word, "control") == 0)
        CreateControl(line);
    else if (strcmp(word, "global_http_check") == 0)
        CreateGlobalHttpCheck(line);
    else if (strcmp(word, "access_logger") == 0) {
        if (line.SkipSymbol('{')) {
            line.ExpectEnd();

            SetChild(std::make_unique<AccessLogConfigParser>());
        } else
            /* <12.0.32 legacy */
            config.access_log.SetLegacy(line.ExpectValueAndEnd());
    } else
        throw LineParser::Error("Unknown option");
}

void
LbConfigParser::FinishChild(std::unique_ptr<ConfigParser> &&c)
{
    if (auto *al = dynamic_cast<AccessLogConfigParser *>(c.get())) {
        config.access_log = al->GetConfig();
    }
}

void
LoadConfigFile(LbConfig &config, const char *path)
{
    LbConfigParser parser(config);
    VariableConfigParser v_parser(parser);
    CommentConfigParser parser2(v_parser);
    IncludeConfigParser parser3(path, parser2);

    ParseConfigFile(path, parser3);
}
