/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONFIG_HXX
#define BENG_PROXY_CONFIG_HXX

#include "util/TrivialArray.hxx"

#include <daemon/user.h>

#include <stddef.h>

struct pool;
struct AddressList;

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct ListenerConfig {
    struct addrinfo *address;

    // TODO: free this string
    char *tag;
};

struct config {
    static constexpr unsigned MAX_PORTS = 32;
    static constexpr unsigned MAX_LISTEN = 32;

    struct daemon_user user;

    TrivialArray<unsigned, MAX_PORTS> ports;

    TrivialArray<ListenerConfig, MAX_LISTEN> listen;

    const char *session_cookie;

    bool dynamic_session_cookie;

    unsigned session_idle_timeout;

    const char *session_save_path;

    const char *control_listen, *multicast_group;

    const char *document_root;

    const char *translation_socket;

    const char *access_logger;

    AddressList *memcached_server;

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path;

    unsigned num_workers;

    /** maximum number of simultaneous connections */
    unsigned max_connections;

    size_t http_cache_size;

    size_t filter_cache_size;

#ifdef HAVE_LIBNFS
    size_t nfs_cache_size;
#endif

    unsigned translate_cache_size;
    unsigned translate_stock_limit;

    unsigned tcp_stock_limit;

    unsigned fcgi_stock_limit, fcgi_stock_max_idle;

    unsigned was_stock_limit, was_stock_max_idle;

    unsigned cluster_size, cluster_node;

    /**
     * Dump widget trees to the log file?
     */
    bool dump_widget_tree;

    bool verbose_response;
};

void
parse_cmdline(struct config *config, struct pool *pool, int argc, char **argv);

#endif
