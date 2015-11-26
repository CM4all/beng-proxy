/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_config.hxx"
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
        MONITOR,
        NODE,
        CLUSTER,
        BRANCH,
        LISTENER,
    } state;

    LbControlConfig *control;
    LbMonitorConfig *monitor;
    LbNodeConfig *node;
    LbClusterConfig *cluster;
    LbBranchConfig *branch;
    LbListenerConfig *listener;

    explicit ConfigParser(LbConfig &_config)
        :config(_config),
         state(State::ROOT) {}
};

static bool
is_word_char(char ch) {
    return IsAlphaNumericASCII(ch) || ch == '_';
}

static bool
is_unquoted_char(char ch) {
    return is_word_char(ch) ||
        ch == '.' || ch == '-' || ch == ':';
}

static char *
fast_strip(char *p)
{
    p = StripLeft(p);
    StripRight(p);
    return p;
}

static const char *
next_word(char **pp)
{
    char *p = *pp;
    if (!is_word_char(*p))
        return nullptr;

    const char *result = p;
    do {
        ++p;
    } while (is_word_char(*p));

    if (IsWhitespaceNotNull(*p)) {
        *p++ = 0;
        p = StripLeft(p);
    } else if (*p != 0)
        return nullptr;

    *pp = p;
    return result;
}

static char *
next_unquoted_value(char **pp)
{
    char *p = *pp;
    if (!is_unquoted_char(*p))
        return nullptr;

    char *result = p;
    do {
        ++p;
    } while (is_unquoted_char(*p));

    if (IsWhitespaceNotNull(*p)) {
        *p++ = 0;
        p = StripLeft(p);
    } else if (*p != 0)
        return nullptr;

    *pp = p;
    return result;
}

static char *
next_value(char **pp)
{
    char *result = next_unquoted_value(pp);
    if (result != nullptr)
        return result;

    char *p = *pp;
    char stop;
    if (*p == '"' || *p == '\'')
        stop = *p;
    else
        return nullptr;

    ++p;
    char *q = strchr(p, stop);
    if (q == nullptr)
        return nullptr;

    *q++ = 0;
    *pp = StripLeft(q);
    return p;
}

static const char *
next_unescape(char **pp)
{
    char *p = *pp;
    char stop;
    if (*p == '"' || *p == '\'')
        stop = *p;
    else
        return nullptr;

    char *dest = ++p;
    const char *value = dest;

    while (true) {
        char ch = *p++;

        if (ch == 0)
            return nullptr;
        else if (ch == stop) {
            *dest = 0;
            *pp = StripLeft(p + 1);
            return value;
        } else if (ch == '\\') {
            ch = *p++;

            switch (ch) {
            case 'r':
                *dest++ = '\r';
                break;

            case 'n':
                *dest++ = '\n';
                break;

            case '\\':
            case '\'':
            case '\"':
                *dest++ = ch;
                break;

            default:
                return nullptr;
            }
        } else
            *dest++ = ch;
    }
}

static bool
next_bool(char **pp)
{
    const char *value = next_value(pp);
    if (value == nullptr)
        throw std::runtime_error("yes/no expected");

    if (strcmp(value, "yes") == 0)
        return true;
    else if (strcmp(value, "no") == 0)
        return false;
    else
        throw std::runtime_error("yes/no expected");
}

static unsigned
next_positive_integer(char **pp)
{
    const char *string = next_value(pp);
    if (string == nullptr)
        return 0;

    char *endptr;
    unsigned long l = strtoul(string, &endptr, 10);
    if (endptr == string || *endptr != 0)
        return 0;

    return (unsigned)l;
}

static bool
expect_eol(char *p)
{
    p = StripLeft(p);
    return *p == 0;
}

static bool
expect_symbol_and_eol(char *p, char symbol)
{
    if (*p != symbol)
        return false;

    return expect_eol(p + 1);
}

static void
config_parser_create_control(ConfigParser *parser, char *p)
{
    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

    auto *control = new LbControlConfig();

    parser->state = ConfigParser::State::CONTROL;
    parser->control = control;
}

static void
config_parser_feed_control(ConfigParser *parser, char *p)
{
    auto *control = parser->control;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (control->bind_address.IsNull())
            throw std::runtime_error("Bind address is missing");

        parser->config.controls.emplace_back(std::move(*control));
        delete control;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "bind") == 0) {
        const char *address = next_value(&p);
        if (address == nullptr)
            throw std::runtime_error("Control address expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        Error error;
        control->bind_address = ParseSocketAddress(address, 80, true, error);
        if (control->bind_address.IsNull())
            throw std::runtime_error(error.GetMessage());
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_create_monitor(ConfigParser *parser, char *p)
{
    const char *name = next_value(&p);
    if (name == nullptr)
        throw std::runtime_error("Monitor name expected");

    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

    if (parser->config.FindMonitor(name) != nullptr)
        throw std::runtime_error("Duplicate monitor name");

    auto *monitor = new LbMonitorConfig(name);

    parser->state = ConfigParser::State::MONITOR;
    parser->monitor = monitor;
}

static void
config_parser_feed_monitor(ConfigParser *parser, char *p)
{
    auto *monitor = parser->monitor;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
            (monitor->expect.empty() && monitor->fade_expect.empty()))
            throw std::runtime_error("No 'expect' string configured");

        parser->config.monitors.insert(std::make_pair(monitor->name,
                                                      *monitor));
        delete monitor;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "type") == 0) {
        const char *value = next_value(&p);
        if (value == nullptr)
            throw std::runtime_error("Monitor address expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        if (monitor->type != LbMonitorConfig::Type::NONE)
            throw std::runtime_error("Monitor type already specified");

        if (strcmp(value, "none") == 0)
            monitor->type = LbMonitorConfig::Type::NONE;
        else if (strcmp(value, "ping") == 0)
            monitor->type = LbMonitorConfig::Type::PING;
        else if (strcmp(value, "connect") == 0)
            monitor->type = LbMonitorConfig::Type::CONNECT;
        else if (strcmp(value, "tcp_expect") == 0)
            monitor->type = LbMonitorConfig::Type::TCP_EXPECT;
        else
            throw std::runtime_error("Unknown monitor type");
    } else if (strcmp(word, "interval") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            throw std::runtime_error("Positive integer expected");

        monitor->interval = value;
    } else if (strcmp(word, "timeout") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            throw std::runtime_error("Positive integer expected");

        monitor->timeout = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "connect_timeout") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            throw std::runtime_error("Positive integer expected");

        monitor->connect_timeout = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "send") == 0) {
        const char *value = next_unescape(&p);
        if (value == nullptr)
            throw std::runtime_error("String value expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        monitor->send = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect") == 0) {
        const char *value = next_unescape(&p);
        if (value == nullptr)
            throw std::runtime_error("String value expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        monitor->expect = value;
    } else if (monitor->type == LbMonitorConfig::Type::TCP_EXPECT &&
               strcmp(word, "expect_graceful") == 0) {
        const char *value = next_unescape(&p);
        if (value == nullptr)
            throw std::runtime_error("String value expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        monitor->fade_expect = value;
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_create_node(ConfigParser *parser, char *p)
{
    const char *name = next_value(&p);
    if (name == nullptr)
        throw std::runtime_error("Node name expected");

    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

    if (parser->config.FindNode(name) != nullptr)
        throw std::runtime_error("Duplicate node name");

    auto *node = new LbNodeConfig(name);

    parser->state = ConfigParser::State::NODE;
    parser->node = node;
}

static void
config_parser_feed_node(ConfigParser *parser, char *p)
{
    auto *node = parser->node;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (node->address.IsNull()) {
            Error error;
            node->address = ParseSocketAddress(node->name.c_str(), 80, false,
                                               error);
            if (node->address.IsNull())
                throw std::runtime_error(error.GetMessage());
        }

        parser->config.nodes.insert(std::make_pair(node->name, std::move(*node)));
        delete node;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "address") == 0) {
        const char *value = next_value(&p);
        if (value == nullptr)
            throw std::runtime_error("Node address expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        if (!node->address.IsNull())
            throw std::runtime_error("Duplicate node address");

        Error error;
        node->address = ParseSocketAddress(value, 80, false, error);
        if (node->address.IsNull())
            throw std::runtime_error(error.GetMessage());
    } else if (strcmp(word, "jvm_route") == 0) {
        const char *value = next_value(&p);
        if (value == nullptr)
            throw std::runtime_error("Value expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        if (!node->jvm_route.empty())
            throw std::runtime_error("Duplicate jvm_route");

        node->jvm_route = value;
    } else
        throw std::runtime_error("Unknown option");
}

static LbNodeConfig &
auto_create_node(ConfigParser *parser, const char *name)
{
    Error error;
    auto address = ParseSocketAddress(name, 80, false, error);
    if (address.IsNull())
        throw std::runtime_error(error.GetMessage());

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
config_parser_create_cluster(ConfigParser *parser, char *p)
{
    const char *name = next_value(&p);
    if (name == nullptr)
        throw std::runtime_error("Pool name expected");

    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

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
config_parser_feed_cluster(ConfigParser *parser, char *p)
{
    auto *cluster = parser->cluster;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (parser->config.FindCluster(cluster->name) != nullptr)
            throw std::runtime_error("Duplicate pool name");

        if (cluster->members.empty())
            throw std::runtime_error("Pool has no members");

        if (!validate_protocol_sticky(cluster->protocol, cluster->sticky_mode))
            throw std::runtime_error("Sticky mode not available for this protocol");

        if (cluster->members.size() == 1)
            /* with only one member, a sticky setting doesn't make
               sense */
            cluster->sticky_mode = STICKY_NONE;

        parser->config.clusters.insert(std::make_pair(cluster->name, *cluster));
        delete cluster;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "name") == 0) {
        const char *name = next_value(&p);
        if (name == nullptr)
            throw std::runtime_error("Pool name expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        cluster->name = name;
    } else if (strcmp(word, "sticky") == 0) {
        const char *sticky_mode = next_value(&p);
        if (sticky_mode == nullptr)
            throw std::runtime_error("Sticky mode expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

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
            throw std::runtime_error("Unknown sticky mode");
    } else if (strcmp(word, "session_cookie") == 0) {
        const char *session_cookie = next_value(&p);
        if (session_cookie == nullptr)
            throw std::runtime_error("Cookie name expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        cluster->session_cookie = session_cookie;
    } else if (strcmp(word, "monitor") == 0) {
        const char *name = next_value(&p);
        if (name == nullptr)
            throw std::runtime_error("Monitor name expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        if (cluster->monitor != nullptr)
            throw std::runtime_error("Monitor already specified");

        cluster->monitor = parser->config.FindMonitor(name);
        if (cluster->monitor == nullptr)
            throw std::runtime_error("No such monitor");
    } else if (strcmp(word, "member") == 0) {
        char *name = next_value(&p);
        if (name == nullptr)
            throw std::runtime_error("Member name expected");

        /*
          if (!expect_eol(p))
          throw std::runtime_error("Syntax error");
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
                    throw std::runtime_error("Malformed port");
            } else
                /* node doesn't exist: parse the given member
                   name, auto-create a new node */
                auto_create_member(parser, member, name);
        }
    } else if (strcmp(word, "protocol") == 0) {
        const char *protocol = next_value(&p);
        if (protocol == nullptr)
            throw std::runtime_error("Protocol name expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        if (strcmp(protocol, "http") == 0)
            cluster->protocol = LbProtocol::HTTP;
        else if (strcmp(protocol, "tcp") == 0)
            cluster->protocol = LbProtocol::TCP;
        else
            throw std::runtime_error("Unknown protocol");
    } else if (strcmp(word, "source_address") == 0) {
        const char *address = next_value(&p);
        if (address == nullptr || strcmp(address, "transparent") != 0)
            throw std::runtime_error("\"transparent\" expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        cluster->transparent_source = true;
    } else if (strcmp(word, "mangle_via") == 0) {
        cluster->mangle_via = next_bool(&p);

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");
    } else if (strcmp(word, "fallback") == 0) {
        if (cluster->fallback.IsDefined())
            throw std::runtime_error("Duplicate fallback");

        const char *location = next_value(&p);
        if (strstr(location, "://") != nullptr) {
            if (!expect_eol(p))
                throw std::runtime_error("Syntax error");

            cluster->fallback.location = location;
        } else {
            char *endptr;
            http_status_t status =
                (http_status_t)(unsigned)strtoul(location, &endptr, 10);
            if (*endptr != 0 || !http_status_is_valid(status))
                throw std::runtime_error("Invalid HTTP status code");

            if (http_status_is_empty(status))
                throw std::runtime_error("This HTTP status does not allow a response body");

            const char *message = next_value(&p);
            if (message == nullptr)
                throw std::runtime_error("Message expected");

            if (!expect_eol(p))
                throw std::runtime_error("Syntax error");

            cluster->fallback.status = status;
            cluster->fallback.message = message;
        }
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_create_branch(ConfigParser *parser, char *p)
{
    const char *name = next_value(&p);
    if (name == nullptr)
        throw std::runtime_error("Pool name expected");

    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

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
config_parser_feed_branch(ConfigParser *parser, char *p)
{
    auto &branch = *parser->branch;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (parser->config.FindBranch(branch.name) != nullptr)
            throw std::runtime_error("Duplicate pool/branch name");

        if (!branch.HasFallback())
            throw std::runtime_error("Branch has no fallback");

        if (branch.GetProtocol() != LbProtocol::HTTP)
            throw std::runtime_error("Only HTTP pools allowed in branch");

        parser->config.branches.insert(std::make_pair(branch.name,
                                                      std::move(branch)));
        delete &branch;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "goto") == 0) {
        const char *name = next_value(&p);
        if (name == nullptr)
            throw std::runtime_error("Pool name expected");

        LbGoto destination = parser->config.FindGoto(name);
        if (!destination.IsDefined())
            throw std::runtime_error("No such pool");

        if (*p == 0) {
            if (branch.HasFallback())
                throw std::runtime_error("Fallback already specified");

            if (!branch.conditions.empty() &&
                branch.conditions.front().destination.GetProtocol() != destination.GetProtocol())
                throw std::runtime_error("Protocol mismatch");

            branch.fallback = destination;
            return;
        }

        if (branch.fallback.IsDefined() &&
            branch.fallback.GetProtocol() != destination.GetProtocol())
                throw std::runtime_error("Protocol mismatch");

        const char *if_ = next_word(&p);
        if (if_ == nullptr || strcmp(if_, "if") != 0)
            throw std::runtime_error("'if' or end of line expected");

        if (*p++ != '$')
            throw std::runtime_error("Attribute name starting with '$' expected");

        const char *attribute = next_word(&p);
        if (attribute == nullptr)
            throw std::runtime_error("Attribute name starting with '$' expected");

        LbConditionConfig::Operator op;
        bool negate;

        if (p[0] == '=' && p[1] == '=') {
            op = LbConditionConfig::Operator::EQUALS;
            negate = false;
            p += 2;
        } else if (p[0] == '!' && p[1] == '=') {
            op = LbConditionConfig::Operator::EQUALS;
            negate = true;
            p += 2;
        } else if (p[0] == '=' && p[1] == '~') {
            op = LbConditionConfig::Operator::REGEX;
            negate = false;
            p += 2;
        } else if (p[0] == '!' && p[1] == '~') {
            op = LbConditionConfig::Operator::REGEX;
            negate = true;
            p += 2;
        } else
            throw std::runtime_error("Comparison operator expected");

        if (!IsWhitespaceNotNull(*p++))
            throw std::runtime_error("Syntax error");

        p = StripLeft(p);

        const char *string = next_unescape(&p);
        if (string == nullptr)
            throw std::runtime_error("Regular expression expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        LbAttributeReference a(LbAttributeReference::Type::HEADER, "");
        if (!parse_attribute_reference(a, attribute))
            throw std::runtime_error("Unknown attribute reference");

        UniqueRegex regex;
        if (op == LbConditionConfig::Operator::REGEX) {
            Error error;
            if (!regex.Compile(string, false, false, error))
                throw std::runtime_error(error.GetMessage());
        }

        LbGotoIfConfig gif(regex.IsDefined()
                           ? LbConditionConfig(std::move(a), negate,
                                               std::move(regex))
                           : LbConditionConfig(std::move(a), negate,
                                               string),
                           destination);
        branch.conditions.emplace_back(std::move(gif));
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_create_listener(ConfigParser *parser, char *p)
{
    const char *name = next_value(&p);
    if (name == nullptr)
        throw std::runtime_error("Listener name expected");

    if (!expect_symbol_and_eol(p, '{'))
        throw std::runtime_error("'{' expected");

    auto *listener = new LbListenerConfig(name);

    parser->state = ConfigParser::State::LISTENER;
    parser->listener = listener;
}

static void
config_parser_feed_listener(ConfigParser *parser, char *p)
{
    auto *listener = parser->listener;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            throw std::runtime_error("Syntax error");

        if (parser->config.FindListener(listener->name) != nullptr)
            throw std::runtime_error("Duplicate listener name");

        if (listener->bind_address.IsNull())
            throw std::runtime_error("Listener has no destination");

        if (listener->ssl && !listener->ssl_config.IsValid())
            throw std::runtime_error("Incomplete SSL configuration");

        parser->config.listeners.emplace_back(std::move(*listener));
        delete listener;

        parser->state = ConfigParser::State::ROOT;
        return;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "bind") == 0) {
        const char *address = next_value(&p);
        if (address == nullptr)
            throw std::runtime_error("Listener address expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        Error error;
        listener->bind_address = ParseSocketAddress(address, 80, true,
                                                    error);
        if (listener->bind_address.IsNull())
            throw std::runtime_error(error.GetMessage());
    } else if (strcmp(word, "pool") == 0) {
        const char *name = next_value(&p);
        if (name == nullptr)
            throw std::runtime_error("Pool name expected");

        if (listener->destination.IsDefined())
            throw std::runtime_error("Pool already configured");

        listener->destination = parser->config.FindGoto(name);
        if (!listener->destination.IsDefined())
            throw std::runtime_error("No such pool");
    } else if (strcmp(word, "verbose_response") == 0) {
        bool value = next_bool(&p);

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        listener->verbose_response = value;
    } else if (strcmp(word, "ssl") == 0) {
        bool value = next_bool(&p);

        if (listener->ssl && !value)
            throw std::runtime_error("SSL cannot be disabled at this point");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        listener->ssl = value;
    } else if (strcmp(word, "ssl_cert") == 0) {
        if (!listener->ssl)
            throw std::runtime_error("SSL is not enabled");

        const char *path = next_value(&p);
        if (path == nullptr)
            throw std::runtime_error("Path expected");

        const char *key_path = nullptr;
        if (*p != 0) {
            key_path = next_value(&p);
            if (key_path == nullptr)
                throw std::runtime_error("Path expected");
        }

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        auto &cks = listener->ssl_config.cert_key;
        if (!cks.empty()) {
            auto &front = cks.front();

            if (key_path == nullptr) {
                if (front.cert_file.empty()) {
                    front.cert_file = path;
                    return;
                } else
                    throw std::runtime_error("Certificate already configured");
            } else {
                if (front.cert_file.empty())
                    throw std::runtime_error("Previous certificate missing");
                if (front.key_file.empty())
                    throw std::runtime_error("Previous key missing");
            }
        }

        if (key_path == nullptr)
            key_path = "";

        cks.emplace_back(path, key_path);
    } else if (strcmp(word, "ssl_key") == 0) {
        if (!listener->ssl)
            throw std::runtime_error("SSL is not enabled");

        const char *path = next_value(&p);
        if (path == nullptr)
            throw std::runtime_error("Path expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        auto &cks = listener->ssl_config.cert_key;
        if (!cks.empty()) {
            if (!cks.front().key_file.empty())
                throw std::runtime_error("Key already configured");

            cks.front().key_file = path;
        } else {
            cks.emplace_back(std::string(), path);
        }
    } else if (strcmp(word, "ssl_ca_cert") == 0) {
        if (!listener->ssl)
            throw std::runtime_error("SSL is not enabled");

        if (!listener->ssl_config.ca_cert_file.empty())
            throw std::runtime_error("Certificate already configured");

        const char *path = next_value(&p);
        if (path == nullptr)
            throw std::runtime_error("Path expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");

        listener->ssl_config.ca_cert_file = path;
    } else if (strcmp(word, "ssl_verify") == 0) {
        if (!listener->ssl)
            throw std::runtime_error("SSL is not enabled");

        const char *value = next_value(&p);
        if (value == nullptr)
            throw std::runtime_error("yes/no expected");

        if (strcmp(value, "yes") == 0)
            listener->ssl_config.verify = SslVerify::YES;
        else if (strcmp(value, "no") == 0)
            listener->ssl_config.verify = SslVerify::NO;
        else if (strcmp(value, "optional") == 0)
            listener->ssl_config.verify = SslVerify::OPTIONAL;
        else
            throw std::runtime_error("yes/no expected");

        if (!expect_eol(p))
            throw std::runtime_error("Syntax error");
    } else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_feed_root(ConfigParser *parser, char *p)
{
    if (*p == '{')
        throw std::runtime_error("Syntax error");

    const char *word = next_word(&p);
    if (word == nullptr)
        throw std::runtime_error("Syntax error");

    if (strcmp(word, "node") == 0)
        config_parser_create_node(parser, p);
    else if (strcmp(word, "pool") == 0)
        config_parser_create_cluster(parser, p);
    else if (strcmp(word, "branch") == 0)
        config_parser_create_branch(parser, p);
    else if (strcmp(word, "listener") == 0)
        config_parser_create_listener(parser, p);
    else if (strcmp(word, "monitor") == 0)
        config_parser_create_monitor(parser, p);
    else if (strcmp(word, "control") == 0)
        config_parser_create_control(parser, p);
    else
        throw std::runtime_error("Unknown option");
}

static void
config_parser_feed(ConfigParser *parser, char *line)
{
    if (*line == '#' || *line == 0)
        return;

    switch (parser->state) {
    case ConfigParser::State::ROOT:
        config_parser_feed_root(parser, line);
        break;

    case ConfigParser::State::CONTROL:
        config_parser_feed_control(parser, line);
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
        line = fast_strip(line);

        try {
            config_parser_feed(&parser, line);
        } catch (const std::runtime_error &e) {
            throw std::runtime_error("Line " + std::to_string(i)
                                     + ": " + e.what());
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
            throw std::runtime_error("Too many members");
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
