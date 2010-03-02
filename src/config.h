/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CONFIG_H
#define __BENG_CONFIG_H

#include "pool.h"

#include <sys/types.h>
#include <stdbool.h>

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
    unsigned ports[MAX_PORTS];
    unsigned num_ports;

    struct addrinfo *listen[MAX_LISTEN];
    unsigned num_listen;

    const char *document_root;

    const char *translation_socket;

    struct uri_with_address *memcached_server;

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

    /**
     * Use the splice() system call?
     */
    bool enable_splice;
};

void
parse_cmdline(struct config *config, pool_t pool, int argc, char **argv);

#endif
