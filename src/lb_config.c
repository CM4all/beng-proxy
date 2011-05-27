/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_config.h"
#include "pool.h"
#include "address-string.h"
#include "address-envelope.h"
#include "address-edit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>

struct config_parser {
    struct lb_config *config;

    enum {
        STATE_ROOT,
        STATE_NODE,
        STATE_CLUSTER,
        STATE_LISTENER,

        STATE_XXX, /* XXX delete this */
    } state;

    struct lb_node_config *node;
    struct lb_cluster_config *cluster;
    struct lb_listener_config *listener;
};

static void
config_parser_init(struct config_parser *parser, struct lb_config *config)
{
    parser->config = config;
    parser->state = STATE_ROOT;
}

static bool
throw(GError **error_r, const char *msg)
{
    g_set_error(error_r, lb_config_quark(), 0, msg);
    return false;
}

static bool
syntax_error(GError **error_r)
{
    return throw(error_r, "Syntax error");
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

static const char *
next_unquoted_value(char **pp)
{
    char *p = *pp;
    if (!is_unquoted_char(*p))
        return NULL;

    const char *result = p;
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

static const char *
next_value(char **pp)
{
    const char *result = next_unquoted_value(pp);
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
config_parser_create_node(struct config_parser *parser, char *p,
                          GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return throw(error_r, "Node name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return throw(error_r, "'{' expected");

    if (lb_config_find_node(parser->config, name) != NULL)
        return throw(error_r, "Duplicate node name");

    struct lb_node_config *node =
        p_malloc(parser->config->pool, sizeof(*node));
    node->name = p_strdup(parser->config->pool, name);
    node->envelope = NULL;

    parser->state = STATE_NODE;
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
            node->envelope = address_envelope_parse(parser->config->pool,
                                                    node->name, 80);
            if (node->envelope == NULL)
                return throw(error_r, "Could not parse node address from name");
        }

        list_add(&node->siblings, &parser->config->nodes);
        parser->state = STATE_ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word != NULL) {
        if (strcmp(word, "address") == 0) {
            const char *value = next_value(&p);
            if (value == NULL)
                return throw(error_r, "Node address expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            if (node->envelope != NULL)
                return throw(error_r, "Duplicate node address");

            node->envelope = address_envelope_parse(parser->config->pool,
                                                    value, 80);
            if (node->envelope == NULL)
                return throw(error_r, "Could not parse node address");

            return true;
        } else if (strcmp(word, "monitor") == 0) {
            /* ignore */
            return true;
        } else
            return throw(error_r, "Unknown option");
    } else
        return syntax_error(error_r);
}

static struct lb_node_config *
auto_create_node(struct config_parser *parser, const char *name,
                 GError **error_r)
{
    const struct address_envelope *envelope =
        address_envelope_parse(parser->config->pool, name, 80);
    if (envelope == NULL) {
        g_set_error(error_r, lb_config_quark(), 0,
                    "Failed to parse node address");
        return NULL;
    }

    struct lb_node_config *node =
        p_malloc(parser->config->pool, sizeof(*node));
    node->name = p_strdup(parser->config->pool, name);
    node->envelope = envelope;
    list_add(&node->siblings, &parser->config->nodes);

    return node;
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
        return throw(error_r, "Pool name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return throw(error_r, "'{' expected");

    struct lb_cluster_config *cluster =
        p_malloc(parser->config->pool, sizeof(*cluster));
    cluster->name = p_strdup(parser->config->pool, name);
    cluster->sticky_mode = LB_STICKY_NONE;
    cluster->num_members = 0;

    parser->state = STATE_CLUSTER;
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
    switch (address->sa_family) {
    case AF_INET:
        return ntohs(((const struct sockaddr_in *)address)->sin_port);

    case AF_INET6:
        return ntohs(((const struct sockaddr_in6 *)address)->sin6_port);

    default:
        return 0;
    }
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

static bool
config_parser_feed_cluster(struct config_parser *parser, char *p,
                           GError **error_r)
{
    struct lb_cluster_config *cluster = parser->cluster;

    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        if (lb_config_find_cluster(parser->config, cluster->name) != NULL)
            return throw(error_r, "Duplicate pool name");

        if (cluster->num_members == 0)
            return throw(error_r, "Pool has no members");

        list_add(&cluster->siblings, &parser->config->clusters);
        parser->state = STATE_ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word != NULL) {
        if (strcmp(word, "name") == 0) {
            const char *name = next_value(&p);
            if (name == NULL)
                return throw(error_r, "Pool name expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            cluster->name = p_strdup(parser->config->pool, name);
            return true;
        } else if (strcmp(word, "sticky") == 0) {
            const char *sticky_mode = next_value(&p);
            if (sticky_mode == NULL)
                return throw(error_r, "Sticky mode expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            if (strcmp(sticky_mode, "none") == 0)
                cluster->sticky_mode = LB_STICKY_NONE;
            else if (strcmp(sticky_mode, "session_hash") == 0)
                cluster->sticky_mode = LB_STICKY_SESSION_HASH;
            else
                return throw(error_r, "Unknown sticky mode");

            return true;
        } else if (strcmp(word, "member") == 0) {
            const char *name = next_value(&p);
            if (name == NULL)
                return throw(error_r, "Member name expected");

            /*
            if (!expect_eol(p))
                return syntax_error(error_r);
            */

            if (cluster->num_members >= MAX_CLUSTER_MEMBERS)
                return throw(error_r, "Pool is full");

            struct lb_member_config *member =
                &cluster->members[cluster->num_members++];

            member->port = 0;
            member->node = lb_config_find_node(parser->config, name);
            if (member->node == NULL) {
                char *q = strchr(name, ':');
                if (q != NULL) {
                    *q++ = 0;
                    member->node = lb_config_find_node(parser->config, name);
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
                        return throw(error_r, "Malformed port");
                } else
                    /* node doesn't exist: parse the given member
                       name, auto-create a new node */
                    return auto_create_member(parser, member, name, error_r);
            }

            return true;
        } else if (strcmp(word, "fallback") == 0 ||
                   strcmp(word, "persist") == 0 ||
                   strcmp(word, "monitor") == 0) {
            /* ignore */
            return true;
        } else
            return throw(error_r, "Unknown option");
    } else
        return syntax_error(error_r);
}

static bool
config_parser_create_listener(struct config_parser *parser, char *p,
                              GError **error_r)
{
    const char *name = next_value(&p);
    if (name == NULL)
        return throw(error_r, "Listener name expected");

    if (!expect_symbol_and_eol(p, '{'))
        return throw(error_r, "'{' expected");

    struct lb_listener_config *listener =
        p_malloc(parser->config->pool, sizeof(*listener));
    listener->name = p_strdup(parser->config->pool, name);
    listener->envelope = NULL;
    listener->cluster = NULL;

    parser->state = STATE_LISTENER;
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

        if (lb_config_find_listener(parser->config, listener->name) != NULL)
            return throw(error_r, "Duplicate listener name");

        if (listener->envelope == NULL)
            return throw(error_r, "Listener has no destination");

        list_add(&listener->siblings, &parser->config->listeners);
        parser->state = STATE_ROOT;
        return true;
    }

    const char *word = next_word(&p);
    if (word != NULL) {
        if (strcmp(word, "bind") == 0) {
            const char *address = next_value(&p);
            if (address == NULL)
                return throw(error_r, "Listener address expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            listener->envelope = address_envelope_parse(parser->config->pool,
                                                        address, 80);
            if (listener->envelope == NULL)
                return throw(error_r, "Could not parse listener address");

            return true;
        } else if (strcmp(word, "pool") == 0) {
            const char *name = next_value(&p);
            if (name == NULL)
                return throw(error_r, "Pool name expected");

            if (!expect_eol(p))
                return syntax_error(error_r);

            if (listener->cluster != NULL)
                return throw(error_r, "Pool already configured");

            listener->cluster = lb_config_find_cluster(parser->config, name);
            if (listener->cluster == NULL)
                return throw(error_r, "No such pool");

            return true;
        } else if (strcmp(word, "protocol") == 0 ||
                   strcmp(word, "profile") == 0 ||
                   strcmp(word, "persist") == 0 ||
                   strcmp(word, "vlans") == 0) {
            return true;
        } else
            return throw(error_r, "Unknown option");
    } else
        return syntax_error(error_r);
}

static bool
config_parser_create_xxx(struct config_parser *parser)
{
    parser->state = STATE_XXX;
    return true;
}

static bool
config_parser_feed_xxx(struct config_parser *parser, char *p,
                       GError **error_r)
{
    if (*p == '}') {
        if (!expect_eol(p + 1))
            return syntax_error(error_r);

        parser->state = STATE_ROOT;
        return true;
    }

    return true;
}

static bool
config_parser_feed_root(struct config_parser *parser, char *p,
                        GError **error_r)
{
    if (*p == '{')
        return syntax_error(error_r);

    const char *word = next_word(&p);
    if (word != NULL) {
        if (strcmp(word, "node") == 0)
            return config_parser_create_node(parser, p, error_r);
        else if (strcmp(word, "pool") == 0)
            return config_parser_create_cluster(parser, p, error_r);
        else if (strcmp(word, "listener") == 0)
            return config_parser_create_listener(parser, p, error_r);
        else if (strcmp(word, "monitor") == 0 ||
                 strcmp(word, "persist") == 0)
            return config_parser_create_xxx(parser);
        else
            return throw(error_r, "Unknown option");
    } else
        return syntax_error(error_r);
}

static bool
config_parser_feed(struct config_parser *parser, char *line,
                   GError **error_r)
{
    if (*line == '#' || *line == 0)
        return true;

    switch (parser->state) {
    case STATE_ROOT:
        return config_parser_feed_root(parser, line, error_r);

    case STATE_NODE:
        return config_parser_feed_node(parser, line, error_r);

    case STATE_CLUSTER:
        return config_parser_feed_cluster(parser, line, error_r);

    case STATE_LISTENER:
        return config_parser_feed_listener(parser, line, error_r);

    case STATE_XXX:
        return config_parser_feed_xxx(parser, line, error_r);
    }

    assert(false);
    return true;
}

static bool
config_parser_run(struct lb_config *config, FILE *file, GError **error_r)
{
    struct config_parser parser;
    config_parser_init(&parser, config);

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
lb_cluster_config_finish(struct pool *pool, struct lb_cluster_config *config,
                         GError **error_r)
{
    address_list_init(&config->address_list);

    switch (config->sticky_mode) {
    case LB_STICKY_NONE:
        break;

    case LB_STICKY_SESSION_HASH:
        config->address_list.sticky = true;
        break;
    }

    for (unsigned i = 0; i < config->num_members; ++i) {
        struct lb_member_config *member = &config->members[i];
        const struct address_envelope *envelope =
            member->node->envelope;
        const struct sockaddr *address = member->port != 0
            ? sockaddr_set_port(pool, &envelope->address, envelope->length,
                                member->port)
            : &envelope->address;

        if (!address_list_add(pool, &config->address_list,
                              address, envelope->length))
            return throw(error_r, "Too many members");
    }

    return true;
}

static bool
lb_config_finish(struct lb_config *config, GError **error_r)
{
    for (struct lb_cluster_config *cluster = (struct lb_cluster_config *)config->clusters.next;
         &cluster->siblings != &config->clusters;
         cluster = (struct lb_cluster_config *)cluster->siblings.next)
        if (!lb_cluster_config_finish(config->pool, cluster, error_r))
            return false;

    return true;
}

struct lb_config *
lb_config_load(struct pool *pool, const char *path,
               GError **error_r)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "Failed to open file %s: %s",
                    path, strerror(errno));
        return NULL;
    }

    pool = pool_new_linear(pool, "lb_config", 32768);
    struct lb_config *config = p_malloc(pool, sizeof(*config));
    config->pool = pool;
    list_init(&config->nodes);
    list_init(&config->clusters);
    list_init(&config->listeners);

    bool success = config_parser_run(config, file, error_r);
    fclose(file);
    if (!success || !lb_config_finish(config, error_r)) {
        pool_unref(config->pool);
        config = NULL;
    }

    return config;
}

const struct lb_node_config *
lb_config_find_node(const struct lb_config *config, const char *name)
{
    for (const struct lb_node_config *node = (const struct lb_node_config *)config->nodes.next;
         &node->siblings != &config->nodes;
         node = (const struct lb_node_config *)node->siblings.next)
        if (strcmp(node->name, name) == 0)
            return node;

    return NULL;
}

const struct lb_cluster_config *
lb_config_find_cluster(const struct lb_config *config, const char *name)
{
    for (const struct lb_cluster_config *cluster = (const struct lb_cluster_config *)config->clusters.next;
         &cluster->siblings != &config->clusters;
         cluster = (const struct lb_cluster_config *)cluster->siblings.next)
        if (strcmp(cluster->name, name) == 0)
            return cluster;

    return NULL;
}

const struct lb_listener_config *
lb_config_find_listener(const struct lb_config *config, const char *name)
{
    for (const struct lb_listener_config *listener = (const struct lb_listener_config *)config->listeners.next;
         &listener->siblings != &config->listeners;
         listener = (const struct lb_listener_config *)listener->siblings.next)
        if (strcmp(listener->name, name) == 0)
            return listener;

    return NULL;
}
