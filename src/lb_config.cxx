/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_config.hxx"
#include "address_string.hxx"
#include "address_envelope.h"
#include "address_edit.h"
#include "gerrno.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>

struct config_parser {
    struct pool *pool;

    lb_config &config;

    enum class State {
        ROOT,
        CONTROL,
        MONITOR,
        NODE,
        CLUSTER,
        BRANCH,
        LISTENER,
    } state;

    struct lb_control_config *control;
    struct lb_monitor_config *monitor;
    struct lb_node_config *node;
    struct lb_cluster_config *cluster;
    struct lb_branch_config *branch;
    struct lb_listener_config *listener;

    config_parser(struct pool *_pool, lb_config &_config)
        :pool(_pool), config(_config),
         state(State::ROOT) {}
};

static constexpr GRegexCompileFlags regex_compile_flags =
    GRegexCompileFlags(G_REGEX_MULTILINE|G_REGEX_DOTALL|
                       G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                       G_REGEX_OPTIMIZE);

static bool
_throw(GError **error_r, const char *msg)
{
    g_set_error(error_r, lb_config_quark(), 0, "%s", msg);
    return false;
}

static bool
syntax_error(GError **error_r)
{
    return _throw(error_r, "Syntax error");
}

static bool
is_whitespace(char ch)
{
    return ch > 0 && ch <= ' ';
}

static bool
is_word_char(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_';
}

static bool
is_unquoted_char(char ch) {
    return is_word_char(ch) ||
        ch == '.' || ch == '-' || ch == ':';
}

static char *
fast_chug(char *p)
{
    while (is_whitespace(*p))
        ++p;
    return p;
}

static char *
fast_strip(char *p)
{
    return g_strchomp(fast_chug(p));
}

static const char *
next_word(char **pp)
{
    char *p = *pp;
    if (!is_word_char(*p))
        return NULL;

    const char *result = p;
    do {
        ++p;
    } while (is_word_char(*p));

    if (is_whitespace(*p)) {
        *p++ = 0;
        p = fast_chug(p);
    } else if (*p != 0)
        return NULL;

    *pp = p;
    return result;
}

static char *
next_unquoted_value(char **pp)
{
    char *p = *pp;
    if (!is_unquoted_char(*p))
        return NULL;

    char *result = p;
    do {
        ++p;
    } while (is_unquoted_char(*p));

    if (is_whitespace(*p)) {
        *p++ = 0;
        p = fast_chug(p);
    } else if (*p != 0)
        return NULL;

    *pp = p;
    return result;
}

static char *
next_value(char **pp)
{
    char *result = next_unquoted_value(pp);
    if (result != NULL)
        return result;

    char *p = *pp;
    char stop;
    if (*p == '"' || *p == '\'')
        stop = *p;
    else
        return NULL;

    ++p;
    char *q = strchr(p, stop);
    if (q == NULL)
        return NULL;

    *q++ = 0;
    *pp = fast_chug(q);
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
        return NULL;

    char *dest = ++p;
    const char *value = dest;

    while (true) {
        char ch = *p++;

        if (ch == 0)
            return NULL;
        else if (ch == stop) {
            *dest = 0;
            *pp = fast_chug(p + 1);
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
                return NULL;
            }
        } else
            *dest++ = ch;
    }
}

static bool
next_bool(char **pp, bool *value_r, GError **error_r)
{
    const char *value = next_value(pp);
    if (value == NULL)
        return _throw(error_r, "yes/no expected");

    if (strcmp(value, "yes") == 0)
        *value_r = true;
    else if (strcmp(value, "no") == 0)
        *value_r = false;
    else
        return _throw(error_r, "yes/no expected");

    return true;
}

static unsigned
next_positive_integer(char **pp)
{
    const char *string = next_value(pp);
    if (string == NULL)
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
    p = fast_chug(p);
    return *p == 0;
}

static bool
expect_symbol_and_eol(char *p, char symbol)
{
    if (*p != symbol)
        return false;

    return expect_eol(p + 1);
}

static bool
config_parser_create_control(struct config_parser *parser, char *p,
                             GError **error_r)
{
    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    lb_control_config *control = new lb_control_config();

    parser->state = config_parser::State::CONTROL;
    parser->control = control;
    return true;
}

static bool
config_parser_feed_control(struct config_parser *parser, char *p,
                           GError **error_r)
{
    struct lb_control_config *control = parser->control;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (control->envelope == NULL)
            return _throw(error_r, "Bind address is missing");

        parser->config.controls.emplace_back(std::move(*control));
        delete control;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "bind") == 0) {
        const char *address = next_value(&p);
        if (address == NULL)
            return _throw(error_r, "Control address expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        control->envelope = address_envelope_parse(parser->pool,
                                                   address, 80, true,
                                                   error_r);
        if (control->envelope == NULL)
            return false;

        return true;
    } else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_create_monitor(struct config_parser *parser, char *p,
                             GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return _throw(error_r, "Monitor name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    if (parser->config.FindMonitor(name) != nullptr)
        return _throw(error_r, "Duplicate monitor name");

    lb_monitor_config *monitor = new lb_monitor_config(name);

    parser->state = config_parser::State::MONITOR;
    parser->monitor = monitor;
    return true;
}

static bool
config_parser_feed_monitor(struct config_parser *parser, char *p,
                           GError **error_r)
{
    struct lb_monitor_config *monitor = parser->monitor;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (monitor->type == lb_monitor_config::Type::TCP_EXPECT &&
            (monitor->expect.empty() && monitor->fade_expect.empty()))
            return _throw(error_r, "No 'expect' string configured");

        parser->config.monitors.insert(std::make_pair(monitor->name,
                                                      *monitor));
        delete monitor;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "type") == 0) {
        const char *value = next_value(&p);
        if (value == NULL)
            return _throw(error_r, "Monitor address expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        if (monitor->type != lb_monitor_config::Type::NONE)
            return _throw(error_r, "Monitor type already specified");

        if (strcmp(value, "none") == 0)
            monitor->type = lb_monitor_config::Type::NONE;
        else if (strcmp(value, "ping") == 0)
            monitor->type = lb_monitor_config::Type::PING;
        else if (strcmp(value, "connect") == 0)
            monitor->type = lb_monitor_config::Type::CONNECT;
        else if (strcmp(value, "tcp_expect") == 0)
            monitor->type = lb_monitor_config::Type::TCP_EXPECT;
        else
            return _throw(error_r, "Unknown monitor type");

        return true;
    } else if (strcmp(word, "interval") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            return _throw(error_r, "Positive integer expected");

        monitor->interval = value;
        return true;
    } else if (strcmp(word, "timeout") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            return _throw(error_r, "Positive integer expected");

        monitor->timeout = value;
        return true;
    } else if (monitor->type == lb_monitor_config::Type::TCP_EXPECT &&
               strcmp(word, "connect_timeout") == 0) {
        unsigned value = next_positive_integer(&p);
        if (value == 0)
            return _throw(error_r, "Positive integer expected");

        monitor->connect_timeout = value;
        return true;
    } else if (monitor->type == lb_monitor_config::Type::TCP_EXPECT &&
               strcmp(word, "send") == 0) {
        const char *value = next_unescape(&p);
        if (value == NULL)
            return _throw(error_r, "String value expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        monitor->send = value;
        return true;
    } else if (monitor->type == lb_monitor_config::Type::TCP_EXPECT &&
               strcmp(word, "expect") == 0) {
        const char *value = next_unescape(&p);
        if (value == NULL)
            return _throw(error_r, "String value expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        monitor->expect = value;
        return true;
    } else if (monitor->type == lb_monitor_config::Type::TCP_EXPECT &&
               strcmp(word, "expect_graceful") == 0) {
        const char *value = next_unescape(&p);
        if (value == NULL)
            return _throw(error_r, "String value expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        monitor->fade_expect = value;
        return true;
    } else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_create_node(struct config_parser *parser, char *p,
                          GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return _throw(error_r, "Node name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    if (parser->config.FindNode(name) != nullptr)
        return _throw(error_r, "Duplicate node name");

    lb_node_config *node = new lb_node_config(name);

    parser->state = config_parser::State::NODE;
    parser->node = node;
    return true;
}

static bool
config_parser_feed_node(struct config_parser *parser, char *p,
                        GError **error_r)
{
    struct lb_node_config *node = parser->node;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (node->envelope == NULL) {
            node->envelope = address_envelope_parse(parser->pool,
                                                    node->name.c_str(),
                                                    80, false,
                                                    error_r);
            if (node->envelope == NULL)
                return false;
        }

        parser->config.nodes.insert(std::make_pair(node->name, *node));
        delete node;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "address") == 0) {
        const char *value = next_value(&p);
        if (value == NULL)
            return _throw(error_r, "Node address expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        if (node->envelope != NULL)
            return _throw(error_r, "Duplicate node address");

        node->envelope = address_envelope_parse(parser->pool,
                                                value, 80, false, error_r);
        if (node->envelope == NULL)
            return false;

        return true;
    } else if (strcmp(word, "jvm_route") == 0) {
        const char *value = next_value(&p);
        if (value == NULL)
            return _throw(error_r, "Value expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        if (!node->jvm_route.empty())
            return _throw(error_r, "Duplicate jvm_route");

        node->jvm_route = value;
        return true;
    } else
        return _throw(error_r, "Unknown option");
}

static struct lb_node_config *
auto_create_node(struct config_parser *parser, const char *name,
                 GError **error_r)
{
    const struct address_envelope *envelope =
        address_envelope_parse(parser->pool, name, 80, false, error_r);
    if (envelope == NULL)
        return NULL;

    lb_node_config node(name, envelope);
    auto i = parser->config.nodes.insert(std::make_pair(name, node));

    return &i.first->second;
}

static bool
auto_create_member(struct config_parser *parser,
                   struct lb_member_config *member,
                   const char *name, GError **error_r)
{
    struct lb_node_config *node =
        auto_create_node(parser, name, error_r);
    if (node == NULL)
        return false;

    member->node = node;
    member->port = 0;
    return true;
}

static bool
config_parser_create_cluster(struct config_parser *parser, char *p,
                             GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return _throw(error_r, "Pool name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    lb_cluster_config *cluster = new lb_cluster_config(name);

    parser->state = config_parser::State::CLUSTER;
    parser->cluster = cluster;
    return true;
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
parse_port(const char *p, const struct address_envelope *envelope)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = envelope->address.sa_family;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *ai;
    if (getaddrinfo(NULL, p, &hints, &ai) != 0)
        return 0;

    unsigned port = sockaddr_port(ai->ai_addr);
    freeaddrinfo(ai);
    return port;
}

gcc_pure
static bool
validate_protocol_sticky(enum lb_protocol protocol, enum sticky_mode sticky)
{
    switch (protocol) {
    case LB_PROTOCOL_HTTP:
        return true;

    case LB_PROTOCOL_TCP:
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

static bool
config_parser_feed_cluster(struct config_parser *parser, char *p,
                           GError **error_r)
{
    struct lb_cluster_config *cluster = parser->cluster;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (parser->config.FindCluster(cluster->name) != NULL)
            return _throw(error_r, "Duplicate pool name");

        if (cluster->members.empty())
            return _throw(error_r, "Pool has no members");

        if (!validate_protocol_sticky(cluster->protocol, cluster->sticky_mode))
            return _throw(error_r, "Sticky mode not available for this protocol");

        if (cluster->members.size() == 1)
            /* with only one member, a sticky setting doesn't make
               sense */
            cluster->sticky_mode = STICKY_NONE;

        parser->config.clusters.insert(std::make_pair(cluster->name, *cluster));
        delete cluster;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "name") == 0) {
        const char *name = next_value(&p);
        if (name == NULL)
            return _throw(error_r, "Pool name expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        cluster->name = name;
        return true;
    } else if (strcmp(word, "sticky") == 0) {
        const char *sticky_mode = next_value(&p);
        if (sticky_mode == NULL)
            return _throw(error_r, "Sticky mode expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

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
            return _throw(error_r, "Unknown sticky mode");

        return true;
    } else if (strcmp(word, "session_cookie") == 0) {
        const char *session_cookie = next_value(&p);
        if (session_cookie == NULL)
            return _throw(error_r, "Cookie name expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        cluster->session_cookie = session_cookie;
        return true;
    } else if (strcmp(word, "monitor") == 0) {
        const char *name = next_value(&p);
        if (name == NULL)
            return _throw(error_r, "Monitor name expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        if (cluster->monitor != NULL)
            return _throw(error_r, "Monitor already specified");

        cluster->monitor = parser->config.FindMonitor(name);
        if (cluster->monitor == NULL)
            return _throw(error_r, "No such monitor");

        return true;
    } else if (strcmp(word, "member") == 0) {
        char *name = next_value(&p);
        if (name == NULL)
            return _throw(error_r, "Member name expected");

        /*
          if (!expect_eol(p))
          return syntax_error(error_r);
        */

        cluster->members.emplace_back();

        struct lb_member_config *member = &cluster->members.back();

        member->node = parser->config.FindNode(name);
        if (member->node == NULL) {
            char *q = strchr(name, ':');
            if (q != NULL) {
                *q++ = 0;
                member->node = parser->config.FindNode(name);
                if (member->node == NULL) {
                    /* node doesn't exist: parse the given member
                       name, auto-create a new node */

                    /* restore the colon */
                    *--q = ':';

                    return auto_create_member(parser, member, name,
                                              error_r);
                }

                member->port = parse_port(q, member->node->envelope);
                if (member->port == 0)
                    return _throw(error_r, "Malformed port");
            } else
                /* node doesn't exist: parse the given member
                   name, auto-create a new node */
                return auto_create_member(parser, member, name, error_r);
        }

        return true;
    } else if (strcmp(word, "protocol") == 0) {
        const char *protocol = next_value(&p);
        if (protocol == NULL)
            return _throw(error_r, "Protocol name expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        if (strcmp(protocol, "http") == 0)
            cluster->protocol = LB_PROTOCOL_HTTP;
        else if (strcmp(protocol, "tcp") == 0)
            cluster->protocol = LB_PROTOCOL_TCP;
        else
            return _throw(error_r, "Unknown protocol");

        return true;
    } else if (strcmp(word, "source_address") == 0) {
        const char *address = next_value(&p);
        if (address == NULL || strcmp(address, "transparent") != 0)
            return _throw(error_r, "\"transparent\" expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        cluster->transparent_source = true;
        return true;
    } else if (strcmp(word, "mangle_via") == 0) {
        if (!next_bool(&p, &cluster->mangle_via, error_r))
            return false;

        if (!expect_eol(p))
            return syntax_error(error_r);

        return true;
    } else if (strcmp(word, "fallback") == 0) {
        if (cluster->fallback.IsDefined())
            return _throw(error_r, "Duplicate fallback");

        const char *location = next_value(&p);
        if (strstr(location, "://") != NULL) {
            if (!expect_eol(p))
                return syntax_error(error_r);

            cluster->fallback.location = location;
            return true;
        } else {
            char *endptr;
            http_status_t status =
                (http_status_t)(unsigned)strtoul(location, &endptr, 10);
            if (*endptr != 0 || !http_status_is_valid(status))
                return _throw(error_r, "Invalid HTTP status code");

            if (http_status_is_empty(status))
                return _throw(error_r,
                              "This HTTP status does not allow a response body");

            const char *message = next_value(&p);
            if (message == NULL)
                return _throw(error_r, "Message expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            cluster->fallback.status = status;
            cluster->fallback.message = message;
            return true;
        }
    } else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_create_branch(struct config_parser *parser, char *p,
                            GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return _throw(error_r, "Pool name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    parser->state = config_parser::State::BRANCH;
    parser->branch = new lb_branch_config(name);
    return true;
}

static bool
parse_attribute_reference(lb_attribute_reference &a, const char *p)
{
    if (strcmp(p, "request_method") == 0) {
        a.type = lb_attribute_reference::Type::METHOD;
        return true;
    } else if (strcmp(p, "request_uri") == 0) {
        a.type = lb_attribute_reference::Type::URI;
        return true;
    } else if (memcmp(p, "http_", 5) == 0) {
        a.type = lb_attribute_reference::Type::HEADER;
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

static bool
config_parser_feed_branch(struct config_parser *parser, char *p,
                          GError **error_r)
{
    lb_branch_config &branch = *parser->branch;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (parser->config.FindBranch(branch.name) != nullptr)
            return _throw(error_r, "Duplicate pool/branch name");

        if (!branch.HasFallback())
            return _throw(error_r, "Branch has no fallback");

        if (branch.GetProtocol() != LB_PROTOCOL_HTTP)
            return _throw(error_r, "Only HTTP pools allowed in branch");

        parser->config.branches.insert(std::make_pair(branch.name, branch));
        delete &branch;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "goto") == 0) {
        const char *name = next_value(&p);
        if (name == NULL)
            return _throw(error_r, "Pool name expected");

        lb_goto destination = parser->config.FindGoto(name);
        if (!destination.IsDefined())
            return _throw(error_r, "No such pool");

        if (*p == 0) {
            if (branch.HasFallback())
                return _throw(error_r, "Fallback already specified");

            if (!branch.conditions.empty() &&
                branch.conditions.front().destination.GetProtocol() != destination.GetProtocol())
                return _throw(error_r, "Protocol mismatch");

            branch.fallback = destination;

            return true;
        }

        if (branch.fallback.IsDefined() &&
            branch.fallback.GetProtocol() != destination.GetProtocol())
                return _throw(error_r, "Protocol mismatch");

        const char *if_ = next_word(&p);
        if (if_ == nullptr || strcmp(if_, "if") != 0)
            return _throw(error_r, "'if' or end of line expected");

        if (*p++ != '$')
            return _throw(error_r, "Attribute name starting with '$' expected");

        const char *attribute = next_word(&p);
        if (attribute == nullptr)
            return _throw(error_r, "Attribute name starting with '$' expected");

        lb_condition_config::Operator op;
        bool negate;

        if (p[0] == '=' && p[1] == '=') {
            op = lb_condition_config::Operator::EQUALS;
            negate = false;
            p += 2;
        } else if (p[0] == '!' && p[1] == '=') {
            op = lb_condition_config::Operator::EQUALS;
            negate = true;
            p += 2;
        } else if (p[0] == '=' && p[1] == '~') {
            op = lb_condition_config::Operator::REGEX;
            negate = false;
            p += 2;
        } else if (p[0] == '!' && p[1] == '~') {
            op = lb_condition_config::Operator::REGEX;
            negate = true;
            p += 2;
        } else
            return _throw(error_r, "Comparison operator expected");

        if (!is_whitespace(*p++))
            return syntax_error(error_r);

        p = fast_chug(p);

        const char *string = next_unescape(&p);
        if (string == nullptr)
            return _throw(error_r, "Regular expression expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        lb_attribute_reference a(lb_attribute_reference::Type::HEADER, "");
        if (!parse_attribute_reference(a, attribute))
            return _throw(error_r, "Unknown attribute reference");

        GRegex *regex = nullptr;
        if (op == lb_condition_config::Operator::REGEX) {
            regex = g_regex_new(string, regex_compile_flags,
                                GRegexMatchFlags(0), error_r);
            if (regex == nullptr)
                return false;
        }

        lb_goto_if_config gif(regex != nullptr
                              ? lb_condition_config(std::move(a), negate,
                                                    regex)
                              : lb_condition_config(std::move(a), negate,
                                                    string),
                              destination);
        branch.conditions.emplace_back(std::move(gif));

        return true;
    } else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_create_listener(struct config_parser *parser, char *p,
                              GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return _throw(error_r, "Listener name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return _throw(error_r, "'{' expected");

    lb_listener_config *listener = new lb_listener_config(name);

    parser->state = config_parser::State::LISTENER;
    parser->listener = listener;
    return true;
}

static bool
config_parser_feed_listener(struct config_parser *parser, char *p,
                            GError **error_r)
{
    struct lb_listener_config *listener = parser->listener;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (parser->config.FindListener(listener->name) != nullptr)
            return _throw(error_r, "Duplicate listener name");

        if (listener->envelope == NULL)
            return _throw(error_r, "Listener has no destination");

        if (listener->ssl && !listener->ssl_config.IsValid())
            return _throw(error_r, "Incomplete SSL configuration");

        parser->config.listeners.emplace_back(std::move(*listener));
        delete listener;

        parser->state = config_parser::State::ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "bind") == 0) {
        const char *address = next_value(&p);
        if (address == NULL)
            return _throw(error_r, "Listener address expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        listener->envelope = address_envelope_parse(parser->pool,
                                                    address, 80, true,
                                                    error_r);
        if (listener->envelope == NULL)
            return false;

        return true;
    } else if (strcmp(word, "pool") == 0) {
        const char *name = next_value(&p);
        if (name == NULL)
            return _throw(error_r, "Pool name expected");

        if (listener->destination.IsDefined())
            return _throw(error_r, "Pool already configured");

        listener->destination = parser->config.FindGoto(name);
        if (!listener->destination.IsDefined())
            return _throw(error_r, "No such pool");

        return true;
    } else if (strcmp(word, "ssl") == 0) {
        bool value = false;
        if (!next_bool(&p, &value, error_r))
            return false;

        if (listener->ssl && !value)
            return _throw(error_r, "SSL cannot be disabled at this point");

        if (!expect_eol(p))
            return syntax_error(error_r);

        listener->ssl = value;
        return true;
    } else if (strcmp(word, "ssl_cert") == 0) {
        if (!listener->ssl)
            return _throw(error_r, "SSL is not enabled");

        const char *path = next_value(&p);
        if (path == NULL)
            return _throw(error_r, "Path expected");

        const char *key_path = NULL;
        if (*p != 0) {
            key_path = next_value(&p);
            if (key_path == NULL)
                return _throw(error_r, "Path expected");
        }

        if (!expect_eol(p))
            return syntax_error(error_r);

        auto &cks = listener->ssl_config.cert_key;
        if (!cks.empty()) {
            auto &front = cks.front();

            if (key_path == nullptr) {
                if (front.cert_file.empty()) {
                    front.cert_file = path;
                    return true;
                } else
                    return _throw(error_r, "Certificate already configured");
            } else {
                if (front.cert_file.empty())
                    return _throw(error_r, "Previous certificate missing");
                if (front.key_file.empty())
                    return _throw(error_r, "Previous key missing");
            }
        }

        if (key_path == nullptr)
            key_path = "";

        cks.emplace_back(path, key_path);
        return true;
    } else if (strcmp(word, "ssl_key") == 0) {
        if (!listener->ssl)
            return _throw(error_r, "SSL is not enabled");

        const char *path = next_value(&p);
        if (path == NULL)
            return _throw(error_r, "Path expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        auto &cks = listener->ssl_config.cert_key;
        if (!cks.empty()) {
            if (!cks.front().key_file.empty())
                return _throw(error_r, "Key already configured");

            cks.front().key_file = path;
        } else {
            cks.emplace_back(std::string(), path);
        }

        return true;
    } else if (strcmp(word, "ssl_ca_cert") == 0) {
        if (!listener->ssl)
            return _throw(error_r, "SSL is not enabled");

        if (!listener->ssl_config.ca_cert_file.empty())
            return _throw(error_r, "Certificate already configured");

        const char *path = next_value(&p);
        if (path == NULL)
            return _throw(error_r, "Path expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        listener->ssl_config.ca_cert_file = path;
        return true;
    } else if (strcmp(word, "ssl_verify") == 0) {
        if (!listener->ssl)
            return _throw(error_r, "SSL is not enabled");

        const char *value = next_value(&p);
        if (value == NULL)
            return _throw(error_r, "yes/no expected");

        if (strcmp(value, "yes") == 0)
            listener->ssl_config.verify = ssl_verify::YES;
        else if (strcmp(value, "no") == 0)
            listener->ssl_config.verify = ssl_verify::NO;
        else if (strcmp(value, "optional") == 0)
            listener->ssl_config.verify = ssl_verify::OPTIONAL;
        else
            return _throw(error_r, "yes/no expected");

        if (!expect_eol(p))
            return syntax_error(error_r);

        return true;
    } else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_feed_root(struct config_parser *parser, char *p,
                        GError **error_r)
{
    if (*p == '{')
        return syntax_error(error_r);

    const char *word = next_word(&p);
    if (word == nullptr)
        return syntax_error(error_r);

    if (strcmp(word, "node") == 0)
        return config_parser_create_node(parser, p, error_r);
    else if (strcmp(word, "pool") == 0)
        return config_parser_create_cluster(parser, p, error_r);
    else if (strcmp(word, "branch") == 0)
        return config_parser_create_branch(parser, p, error_r);
    else if (strcmp(word, "listener") == 0)
        return config_parser_create_listener(parser, p, error_r);
    else if (strcmp(word, "monitor") == 0)
        return config_parser_create_monitor(parser, p, error_r);
    else if (strcmp(word, "control") == 0)
        return config_parser_create_control(parser, p, error_r);
    else
        return _throw(error_r, "Unknown option");
}

static bool
config_parser_feed(struct config_parser *parser, char *line,
                   GError **error_r)
{
    if (*line == '#' || *line == 0)
        return true;

    switch (parser->state) {
    case config_parser::State::ROOT:
        return config_parser_feed_root(parser, line, error_r);

    case config_parser::State::CONTROL:
        return config_parser_feed_control(parser, line, error_r);

    case config_parser::State::MONITOR:
        return config_parser_feed_monitor(parser, line, error_r);

    case config_parser::State::NODE:
        return config_parser_feed_node(parser, line, error_r);

    case config_parser::State::CLUSTER:
        return config_parser_feed_cluster(parser, line, error_r);

    case config_parser::State::BRANCH:
        return config_parser_feed_branch(parser, line, error_r);

    case config_parser::State::LISTENER:
        return config_parser_feed_listener(parser, line, error_r);
    }

    assert(false);
    return true;
}

static bool
config_parser_run(struct pool *pool, lb_config &config, FILE *file, GError **error_r)
{
    config_parser parser(pool, config);

    char buffer[4096], *line;
    unsigned i = 1;
    while ((line = fgets(buffer, sizeof(buffer), file)) != NULL) {
        line = fast_strip(line);
        if (!config_parser_feed(&parser, line, error_r)) {
            g_prefix_error(error_r, "Line %u: ", i);
            return false;
        }

        ++i;
    }

    return true;
}

static bool
lb_cluster_config_finish(struct pool *pool, lb_cluster_config &config,
                         GError **error_r)
{
    config.address_list.Init();
    config.address_list.SetStickyMode(config.sticky_mode);

    for (auto &member : config.members) {
        const struct address_envelope *envelope = member.node->envelope;
        const struct sockaddr *address = member.port != 0
            ? sockaddr_set_port(pool, &envelope->address, envelope->length,
                                member.port)
            : &envelope->address;

        if (!config.address_list.Add(pool, address, envelope->length))
            return _throw(error_r, "Too many members");
    }

    return true;
}

static bool
lb_config_finish(struct pool *pool, lb_config &config, GError **error_r)
{
    for (auto &i : config.clusters)
        if (!lb_cluster_config_finish(pool, i.second, error_r))
            return false;

    return true;
}

struct lb_config *
lb_config_load(struct pool *pool, const char *path, GError **error_r)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        g_set_error(error_r, errno_quark(), errno,
                    "Failed to open file %s: %s",
                    path, strerror(errno));
        return NULL;
    }

    lb_config *config = new lb_config();

    bool success = config_parser_run(pool, *config, file, error_r);
    fclose(file);
    if (!success || !lb_config_finish(pool, *config, error_r)) {
        delete config;
        config = nullptr;
    }

    return config;
}

int
lb_cluster_config::FindJVMRoute(const char *jvm_route) const
{
    assert(jvm_route != NULL);

    for (unsigned i = 0, n = members.size(); i < n; ++i) {
        const lb_node_config &node = *members[i].node;

        if (!node.jvm_route.empty() && node.jvm_route == jvm_route)
            return i;
    }

    return -1;
}
