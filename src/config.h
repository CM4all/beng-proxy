/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CONFIG_H
#define __BENG_CONFIG_H

#include <daemon/user.h>

#include <sys/types.h>
#include <stdbool.h>

struct pool;

enum {
    MAX_PORTS = 32,
    MAX_LISTEN = 32,
};

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct config {
    struct daemon_user user;

    /**
     * The configuration file.  Only used by beng-lb.
     */
    const char *config_path;

    unsigned ports[MAX_PORTS];
    unsigned num_ports;

    struct addrinfo *listen[MAX_LISTEN];
    unsigned num_listen;

    const char *session_cookie;

    bool dynamic_session_cookie;

    unsigned session_idle_timeout;

    const char *session_save_path;

    const char *control_listen, *multicast_group;

    const char *document_root;

    const char *translation_socket;

    const char *access_logger;

    struct address_list *memcached_server;

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path;

    unsigned num_workers;

    /** maximum number of simultaneous connections */
    unsigned max_connections;

    size_t http_cache_size;

    size_t filter_cache_size;

    unsigned translate_cache_size;

    unsigned tcp_stock_limit;

    unsigned fcgi_stock_limit;

    unsigned was_stock_limit;

    unsigned cluster_size, cluster_node;

    /**
     * If true, then the environment (e.g. the configuration file) is
     * checked, and the process exits.
     */
    bool check;

    /**
     * Use the splice() system call?
     */
    bool enable_splice;

    /**
     * Dump widget trees to the log file?
     */
    bool dump_widget_tree;
};

void
parse_cmdline(struct config *config, struct pool *pool, int argc, char **argv);

#endif
