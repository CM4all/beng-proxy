/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_config.hxx"
#include "LineParser.hxx"
#include "address_edit.h"
#include "net/Parser.hxx"
#include "util/Error.hxx"
#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"

#include <system_error>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

struct ConfigParser {
    LbConfig &config;

    enum class State {
        ROOT,
        CONTROL,
        CERT_DB,
        MONITOR,
        NODE,
        CLUSTER,
        BRANCH,
        LISTENER,
    } state;

    LbControlConfig *control;
    LbCertDatabaseConfig *cert_db;
    LbMonitorConfig *monitor;
    LbNodeConfig *node;
    LbClusterConfig *cluster;
    LbBranchConfig *branch;
    LbListenerConfig *listener;

    explicit ConfigParser(LbConfig &_config)
        :config(_config),
         state(State::ROOT) {}
};

static void
config_parser_create_control(ConfigParser *parser, LineParser &line)
{
    line.ExpectSymbolAndEol('{');

    auto *control = new LbControlConfig();

    parser->state = ConfigParser::State::CONTROL;
    parser->control = control;
}

static void
config_parser_feed_control(ConfigParser *parser, LineParser &line)
{
    auto *control = parser->control;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (control->bind_address.IsNull())
            throw LineParser::Error("Bind address is missing");

        parser->config.controls.emplace_back(std::move(*control));
        delete control;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "bind") == 0) {
        const char *address = line.NextValue();
        if (address == nullptr)
            throw LineParser::Error("Control address expected");

        line.ExpectEnd();

        Error error;
        control->bind_address = ParseSocketAddress(address, 80, true, error);
        if (control->bind_address.IsNull())
            throw LineParser::Error(error.GetMessage());
    } else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_create_certdb(ConfigParser &parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw std::runtime_error("Database name expected");

    line.ExpectSymbolAndEol('{');

    if (parser.config.FindCertDb(name) != nullptr)
        throw LineParser::Error("Duplicate certdb name");

    auto *db = new LbCertDatabaseConfig(name);

    parser.state = ConfigParser::State::CERT_DB;
    parser.cert_db = db;
}

static void
config_parser_feed_certdb(ConfigParser &parser, LineParser &line)
{
    auto &db = *parser.cert_db;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        db.Check();

        parser.config.cert_dbs.insert(std::make_pair(db.name, db));
        delete &db;

        parser.state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "connect") == 0) {
        const char *connect = line.NextValue();
        if (connect == nullptr)
            throw std::runtime_error("Connect string expected");

        line.ExpectEnd();

        db.connect = connect;
    } else if (strcmp(word, "schema") == 0) {
        const char *schema = line.NextValue();
        if (schema == nullptr)
            throw std::runtime_error("Schema name expected");

        line.ExpectEnd();

        db.schema = schema;
    } else if (strcmp(word, "ca_cert") == 0) {
        const char *path = line.NextValue();
        if (path == nullptr)
            throw std::runtime_error("CA certificate path name expected");

        line.ExpectEnd();

        db.ca_certs.emplace_back(path);
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_create_monitor(ConfigParser *parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw LineParser::Error("Monitor name expected");

    line.ExpectSymbolAndEol('{');

    if (parser->config.FindMonitor(name) != nullptr)
        throw LineParser::Error("Duplicate monitor name");

    auto *monitor = new LbMonitorConfig(name);

    parser->state = ConfigParser::State::MONITOR;
    parser->monitor = monitor;
}

static void
config_parser_feed_monitor(ConfigParser *parser, LineParser &line)
{
    auto *monitor = parser->monitor;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
            (monitor->expect.empty() && monitor->fade_expect.empty()))
            throw LineParser::Error("No 'expect' string configured");

        parser->config.monitors.insert(std::make_pair(monitor->name,
                                                      *monitor));
        delete monitor;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "type") == 0) {
        const char *value = line.NextValue();
        if (value == nullptr)
            throw LineParser::Error("Monitor address expected");

        line.ExpectEnd();

        if (monitor->type != LbMonitorConfig::Type::NONE)
            throw LineParser::Error("Monitor type already specified");

        if (strcmp(value, "none") == 0)
            monitor->type = LbMonitorConfig::Type::NONE;
        else if (strcmp(value, "ping") == 0)
            monitor->type = LbMonitorConfig::Type::PING;
        else if (strcmp(value, "connect") == 0)
            monitor->type = LbMonitorConfig::Type::CONNECT;
        else if (strcmp(value, "tcp_expect") == 0)
            monitor->type = LbMonitorConfig::Type::TCP_EXPECT;
        else
            throw LineParser::Error("Unknown monitor type");
    } else if (strcmp(word, "interval") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        monitor->interval = value;
    } else if (strcmp(word, "timeout") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        monitor->timeout = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "connect_timeout") == 0) {
        unsigned value = line.NextPositiveInteger();
        if (value == 0)
            throw LineParser::Error("Positive integer expected");

        monitor->connect_timeout = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "send") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        monitor->send = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        monitor->expect = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect_graceful") == 0) {
        const char *value = line.NextUnescape();
        if (value == nullptr)
            throw LineParser::Error("String value expected");

        line.ExpectEnd();

        monitor->fade_expect = value;
    } else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_create_node(ConfigParser *parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw LineParser::Error("Node name expected");

    line.ExpectSymbolAndEol('{');

    if (parser->config.FindNode(name) != nullptr)
        throw LineParser::Error("Duplicate node name");

    auto *node = new LbNodeConfig(name);

    parser->state = ConfigParser::State::NODE;
    parser->node = node;
}

static void
config_parser_feed_node(ConfigParser *parser, LineParser &line)
{
    auto *node = parser->node;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (node->address.IsNull()) {
            Error error;
            node->address = ParseSocketAddress(node->name.c_str(), 80, false,
                                               error);
            if (node->address.IsNull())
                throw LineParser::Error(error.GetMessage());
        }

        parser->config.nodes.insert(std::make_pair(node->name, std::move(*node)));
        delete node;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "address") == 0) {
        const char *value = line.NextValue();
        if (value == nullptr)
            throw LineParser::Error("Node address expected");

        line.ExpectEnd();

        if (!node->address.IsNull())
            throw LineParser::Error("Duplicate node address");

        Error error;
        node->address = ParseSocketAddress(value, 80, false, error);
        if (node->address.IsNull())
            throw LineParser::Error(error.GetMessage());
    } else if (strcmp(word, "jvm_route") == 0) {
        const char *value = line.NextValue();
        if (value == nullptr)
            throw LineParser::Error("Value expected");

        line.ExpectEnd();

        if (!node->jvm_route.empty())
            throw LineParser::Error("Duplicate jvm_route");

        node->jvm_route = value;
    } else
        throw LineParser::Error("Unknown option");
}

static LbNodeConfig &
auto_create_node(ConfigParser *parser, const char *name)
{
    Error error;
    auto address = ParseSocketAddress(name, 80, false, error);
    if (address.IsNull())
        throw LineParser::Error(error.GetMessage());

    LbNodeConfig node(name, std::move(address));
    auto i = parser->config.nodes.insert(std::make_pair(name,
                                                        std::move(node)));
    return i.first->second;
}

static void
auto_create_member(ConfigParser *parser,
                   LbMemberConfig *member,
                   const char *name)
{
    auto &node = auto_create_node(parser, name);
    member->node = &node;
    member->port = 0;
}

static void
config_parser_create_cluster(ConfigParser *parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw LineParser::Error("Pool name expected");

    line.ExpectSymbolAndEol('{');

    auto *cluster = new LbClusterConfig(name);

    parser->state = ConfigParser::State::CLUSTER;
    parser->cluster = cluster;
}

/**
 * Extract the port number from a struct sockaddr.  Returns 0 if not
 * applicable.
 */
static unsigned
sockaddr_port(const struct sockaddr *address)
{
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

    switch (address->sa_family) {
    case AF_INET:
        return ntohs(((const struct sockaddr_in *)address)->sin_port);

    case AF_INET6:
        return ntohs(((const struct sockaddr_in6 *)address)->sin6_port);

    default:
        return 0;
    }

#ifdef __clang__
#pragma GCC diagnostic pop
#endif
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

    unsigned port = sockaddr_port(ai->ai_addr);
    freeaddrinfo(ai);
    return port;
}

gcc_pure
static bool
validate_protocol_sticky(LbProtocol protocol, enum sticky_mode sticky)
{
    switch (protocol) {
    case LbProtocol::HTTP:
        return true;

    case LbProtocol::TCP:
        switch (sticky) {
        case STICKY_NONE:
        case STICKY_FAILOVER:
        case STICKY_SOURCE_IP:
            return true;

        case STICKY_SESSION_MODULO:
        case STICKY_COOKIE:
        case STICKY_JVM_ROUTE:
            return false;
        }
    }

    return false;
}

static void
config_parser_feed_cluster(ConfigParser *parser, LineParser &line)
{
    auto *cluster = parser->cluster;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (parser->config.FindCluster(cluster->name) != nullptr)
            throw LineParser::Error("Duplicate pool name");

        if (cluster->members.empty())
            throw LineParser::Error("Pool has no members");

        if (!validate_protocol_sticky(cluster->protocol, cluster->sticky_mode))
            throw LineParser::Error("Sticky mode not available for this protocol");

        if (cluster->members.size() == 1)
            /* with only one member, a sticky setting doesn't make
               sense */
            cluster->sticky_mode = STICKY_NONE;

        parser->config.clusters.insert(std::make_pair(cluster->name, *cluster));
        delete cluster;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "name") == 0) {
        const char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Pool name expected");

        line.ExpectEnd();

        cluster->name = name;
    } else if (strcmp(word, "sticky") == 0) {
        const char *sticky_mode = line.NextValue();
        if (sticky_mode == nullptr)
            throw LineParser::Error("Sticky mode expected");

        line.ExpectEnd();

        if (strcmp(sticky_mode, "none") == 0)
            cluster->sticky_mode = STICKY_NONE;
        else if (strcmp(sticky_mode, "failover") == 0)
            cluster->sticky_mode = STICKY_FAILOVER;
        else if (strcmp(sticky_mode, "source_ip") == 0)
            cluster->sticky_mode = STICKY_SOURCE_IP;
        else if (strcmp(sticky_mode, "session_modulo") == 0)
            cluster->sticky_mode = STICKY_SESSION_MODULO;
        else if (strcmp(sticky_mode, "cookie") == 0)
            cluster->sticky_mode = STICKY_COOKIE;
        else if (strcmp(sticky_mode, "jvm_route") == 0)
            cluster->sticky_mode = STICKY_JVM_ROUTE;
        else
            throw LineParser::Error("Unknown sticky mode");
    } else if (strcmp(word, "session_cookie") == 0) {
        const char *session_cookie = line.NextValue();
        if (session_cookie == nullptr)
            throw LineParser::Error("Cookie name expected");

        line.ExpectEnd();

        cluster->session_cookie = session_cookie;
    } else if (strcmp(word, "monitor") == 0) {
        const char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Monitor name expected");

        line.ExpectEnd();

        if (cluster->monitor != nullptr)
            throw LineParser::Error("Monitor already specified");

        cluster->monitor = parser->config.FindMonitor(name);
        if (cluster->monitor == nullptr)
            throw LineParser::Error("No such monitor");
    } else if (strcmp(word, "member") == 0) {
        char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Member name expected");

        /*
        line.ExpectEnd();
        */

        cluster->members.emplace_back();

        auto *member = &cluster->members.back();

        member->node = parser->config.FindNode(name);
        if (member->node == nullptr) {
            char *q = strchr(name, ':');
            if (q != nullptr) {
                *q++ = 0;
                member->node = parser->config.FindNode(name);
                if (member->node == nullptr) {
                    /* node doesn't exist: parse the given member
                       name, auto-create a new node */

                    /* restore the colon */
                    *--q = ':';

                    auto_create_member(parser, member, name);
                    return;
                }

                member->port = parse_port(q, member->node->address);
                if (member->port == 0)
                    throw LineParser::Error("Malformed port");
            } else
                /* node doesn't exist: parse the given member
                   name, auto-create a new node */
                auto_create_member(parser, member, name);
        }
    } else if (strcmp(word, "protocol") == 0) {
        const char *protocol = line.NextValue();
        if (protocol == nullptr)
            throw LineParser::Error("Protocol name expected");

        line.ExpectEnd();

        if (strcmp(protocol, "http") == 0)
            cluster->protocol = LbProtocol::HTTP;
        else if (strcmp(protocol, "tcp") == 0)
            cluster->protocol = LbProtocol::TCP;
        else
            throw LineParser::Error("Unknown protocol");
    } else if (strcmp(word, "source_address") == 0) {
        const char *address = line.NextValue();
        if (address == nullptr || strcmp(address, "transparent") != 0)
            throw LineParser::Error("\"transparent\" expected");

        line.ExpectEnd();

        cluster->transparent_source = true;
    } else if (strcmp(word, "mangle_via") == 0) {
        cluster->mangle_via = line.NextBool();

        line.ExpectEnd();
    } else if (strcmp(word, "fallback") == 0) {
        if (cluster->fallback.IsDefined())
            throw LineParser::Error("Duplicate fallback");

        const char *location = line.NextValue();
        if (strstr(location, "://") != nullptr) {
            line.ExpectEnd();

            cluster->fallback.location = location;
        } else {
            char *endptr;
            http_status_t status =
                (http_status_t)(unsigned)strtoul(location, &endptr, 10);
            if (*endptr != 0 || !http_status_is_valid(status))
                throw LineParser::Error("Invalid HTTP status code");

            if (http_status_is_empty(status))
                throw LineParser::Error("This HTTP status does not allow a response body");

            const char *message = line.NextValue();
            if (message == nullptr)
                throw LineParser::Error("Message expected");

            line.ExpectEnd();

            cluster->fallback.status = status;
            cluster->fallback.message = message;
        }
    } else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_create_branch(ConfigParser *parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw LineParser::Error("Pool name expected");

    line.ExpectSymbolAndEol('{');

    parser->state = ConfigParser::State::BRANCH;
    parser->branch = new LbBranchConfig(name);
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

static void
config_parser_feed_branch(ConfigParser *parser, LineParser &line)
{
    auto &branch = *parser->branch;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (parser->config.FindBranch(branch.name) != nullptr)
            throw LineParser::Error("Duplicate pool/branch name");

        if (!branch.HasFallback())
            throw LineParser::Error("Branch has no fallback");

        if (branch.GetProtocol() != LbProtocol::HTTP)
            throw LineParser::Error("Only HTTP pools allowed in branch");

        parser->config.branches.insert(std::make_pair(branch.name,
                                                      std::move(branch)));
        delete &branch;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "goto") == 0) {
        const char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Pool name expected");

        LbGoto destination = parser->config.FindGoto(name);
        if (!destination.IsDefined())
            throw LineParser::Error("No such pool");

        if (line.IsEnd()) {
            if (branch.HasFallback())
                throw LineParser::Error("Fallback already specified");

            if (!branch.conditions.empty() &&
                branch.conditions.front().destination.GetProtocol() != destination.GetProtocol())
                throw LineParser::Error("Protocol mismatch");

            branch.fallback = destination;
            return;
        }

        if (branch.fallback.IsDefined() &&
            branch.fallback.GetProtocol() != destination.GetProtocol())
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
        branch.conditions.emplace_back(std::move(gif));
    } else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_create_listener(ConfigParser *parser, LineParser &line)
{
    const char *name = line.NextValue();
    if (name == nullptr)
        throw LineParser::Error("Listener name expected");

    line.ExpectSymbolAndEol('{');

    auto *listener = new LbListenerConfig(name);

    parser->state = ConfigParser::State::LISTENER;
    parser->listener = listener;
}

static void
config_parser_feed_listener(ConfigParser *parser, LineParser &line)
{
    auto *listener = parser->listener;

    if (line.SkipSymbol('}')) {
        line.ExpectEnd();

        if (parser->config.FindListener(listener->name) != nullptr)
            throw LineParser::Error("Duplicate listener name");

        if (listener->bind_address.IsNull())
            throw LineParser::Error("Listener has no destination");

        if (listener->ssl &&
            !listener->ssl_config.IsValid(listener->cert_db != nullptr))
            throw LineParser::Error("Incomplete SSL configuration");

        parser->config.listeners.emplace_back(std::move(*listener));
        delete listener;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "bind") == 0) {
        const char *address = line.NextValue();
        if (address == nullptr)
            throw LineParser::Error("Listener address expected");

        line.ExpectEnd();

        Error error;
        listener->bind_address = ParseSocketAddress(address, 80, true,
                                                    error);
        if (listener->bind_address.IsNull())
            throw LineParser::Error(error.GetMessage());
    } else if (strcmp(word, "pool") == 0) {
        const char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Pool name expected");

        if (listener->destination.IsDefined())
            throw LineParser::Error("Pool already configured");

        listener->destination = parser->config.FindGoto(name);
        if (!listener->destination.IsDefined())
            throw LineParser::Error("No such pool");
    } else if (strcmp(word, "verbose_response") == 0) {
        bool value = line.NextBool();

        line.ExpectEnd();

        listener->verbose_response = value;
    } else if (strcmp(word, "ssl") == 0) {
        bool value = line.NextBool();

        if (listener->ssl && !value)
            throw LineParser::Error("SSL cannot be disabled at this point");

        line.ExpectEnd();

        listener->ssl = value;
    } else if (strcmp(word, "ssl_cert_db") == 0) {
        if (!listener->ssl)
            throw LineParser::Error("SSL is not enabled");

        if (listener->cert_db != nullptr)
            throw LineParser::Error("ssl_cert_db already set");

        const char *name = line.NextValue();
        if (name == nullptr)
            throw LineParser::Error("Name expected");

        line.ExpectEnd();

        listener->cert_db = parser->config.FindCertDb(name);
        if (listener->cert_db == nullptr)
            throw LineParser::Error(std::string("No such cert_db: ") + name);
    } else if (strcmp(word, "ssl_cert") == 0) {
        if (!listener->ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *path = line.NextValue();
        if (path == nullptr)
            throw LineParser::Error("Path expected");

        const char *key_path = nullptr;
        if (!line.IsEnd()) {
            key_path = line.NextValue();
            if (key_path == nullptr)
                throw LineParser::Error("Path expected");
        }

        line.ExpectEnd();

        auto &cks = listener->ssl_config.cert_key;
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
        if (!listener->ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *path = line.NextValue();
        if (path == nullptr)
            throw LineParser::Error("Path expected");

        line.ExpectEnd();

        auto &cks = listener->ssl_config.cert_key;
        if (!cks.empty()) {
            if (!cks.front().key_file.empty())
                throw LineParser::Error("Key already configured");

            cks.front().key_file = path;
        } else {
            cks.emplace_back(std::string(), path);
        }
    } else if (strcmp(word, "ssl_ca_cert") == 0) {
        if (!listener->ssl)
            throw LineParser::Error("SSL is not enabled");

        if (!listener->ssl_config.ca_cert_file.empty())
            throw LineParser::Error("Certificate already configured");

        const char *path = line.NextValue();
        if (path == nullptr)
            throw LineParser::Error("Path expected");

        line.ExpectEnd();

        listener->ssl_config.ca_cert_file = path;
    } else if (strcmp(word, "ssl_verify") == 0) {
        if (!listener->ssl)
            throw LineParser::Error("SSL is not enabled");

        const char *value = line.NextValue();
        if (value == nullptr)
            throw LineParser::Error("yes/no expected");

        if (strcmp(value, "yes") == 0)
            listener->ssl_config.verify = SslVerify::YES;
        else if (strcmp(value, "no") == 0)
            listener->ssl_config.verify = SslVerify::NO;
        else if (strcmp(value, "optional") == 0)
            listener->ssl_config.verify = SslVerify::OPTIONAL;
        else
            throw LineParser::Error("yes/no expected");

        line.ExpectEnd();
    } else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_feed_root(ConfigParser *parser, LineParser &line)
{
    const char *word = line.NextWord();
    if (word == nullptr)
        throw LineParser::Error("Syntax error");

    if (strcmp(word, "node") == 0)
        config_parser_create_node(parser, line);
    else if (strcmp(word, "pool") == 0)
        config_parser_create_cluster(parser, line);
    else if (strcmp(word, "branch") == 0)
        config_parser_create_branch(parser, line);
    else if (strcmp(word, "listener") == 0)
        config_parser_create_listener(parser, line);
    else if (strcmp(word, "monitor") == 0)
        config_parser_create_monitor(parser, line);
    else if (strcmp(word, "cert_db") == 0)
        config_parser_create_certdb(*parser, line);
    else if (strcmp(word, "control") == 0)
        config_parser_create_control(parser, line);
    else
        throw LineParser::Error("Unknown option");
}

static void
config_parser_feed(ConfigParser *parser, LineParser &line)
{
    if (line.front() == '#' || line.IsEnd())
        return;

    switch (parser->state) {
    case ConfigParser::State::ROOT:
        config_parser_feed_root(parser, line);
        break;

    case ConfigParser::State::CONTROL:
        config_parser_feed_control(parser, line);
        break;

    case ConfigParser::State::CERT_DB:
        config_parser_feed_certdb(*parser, line);
        break;

    case ConfigParser::State::MONITOR:
        config_parser_feed_monitor(parser, line);
        break;

    case ConfigParser::State::NODE:
        config_parser_feed_node(parser, line);
        break;

    case ConfigParser::State::CLUSTER:
        config_parser_feed_cluster(parser, line);
        break;

    case ConfigParser::State::BRANCH:
        config_parser_feed_branch(parser, line);
        break;

    case ConfigParser::State::LISTENER:
        config_parser_feed_listener(parser, line);
        break;
    }
}

static void
config_parser_run(LbConfig &config, FILE *file)
{
    ConfigParser parser(config);

    char buffer[4096], *line;
    unsigned i = 1;
    while ((line = fgets(buffer, sizeof(buffer), file)) != nullptr) {
        LineParser line_parser(line);

        try {
            config_parser_feed(&parser, line_parser);
        } catch (...) {
            std::throw_with_nested(LineParser::Error("Line " + std::to_string(i)));
        }

        ++i;
    }
}

static void
lb_cluster_config_finish(struct pool *pool, LbClusterConfig &config)
{
    config.address_list.Init();
    config.address_list.SetStickyMode(config.sticky_mode);

    for (auto &member : config.members) {
        const AllocatedSocketAddress &node_address = member.node->address;
        const struct sockaddr *address = member.port != 0
            ? sockaddr_set_port(pool, node_address, node_address.GetSize(),
                                member.port)
            : node_address;

        if (!config.address_list.Add(pool, {address, node_address.GetSize()}))
            throw LineParser::Error("Too many members");
    }
}

static void
lb_config_finish(struct pool *pool, LbConfig &config)
{
    for (auto &i : config.clusters)
        lb_cluster_config_finish(pool, i.second);
}

LbConfig
lb_config_load(struct pool *pool, const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == nullptr)
        throw std::system_error(errno, std::system_category(),
                                std::string("Failed to open ") + path);

    LbConfig config;

    config_parser_run(config, file);
    fclose(file);
    lb_config_finish(pool, config);

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
