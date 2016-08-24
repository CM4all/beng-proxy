/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONFIG_HXX
#define BENG_PROXY_CONFIG_HXX

#include "net/AllocatedSocketAddress.hxx"
#include "net/AddressInfo.hxx"
#include "util/StaticArray.hxx"
#include "spawn/Config.hxx"

#include <forward_list>
#include <chrono>

#include <stddef.h>

struct StringView;

struct BpConfig {
    static constexpr unsigned MAX_PORTS = 32;

    StaticArray<unsigned, MAX_PORTS> ports;

    struct Listener {
        AllocatedSocketAddress address;

        std::string tag;

        std::string zeroconf_type;

        Listener() = default;

        Listener(SocketAddress _address, const std::string &_tag)
            :address(_address), tag(_tag) {}
    };

    std::forward_list<Listener> listen;

    std::string session_cookie = "beng_proxy_session";

    std::chrono::seconds session_idle_timeout = std::chrono::minutes(30);

    const char *session_save_path = nullptr;

    AllocatedSocketAddress control_listen;

    const char *multicast_group = nullptr;

    const char *document_root = "/var/www";

    const char *translation_socket = "@translation";

    AddressInfo memcached_server;

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path = nullptr;

    unsigned num_workers = 0;

    /** maximum number of simultaneous connections */
    unsigned max_connections = 8192;

    size_t http_cache_size = 512 * 1024 * 1024;

    size_t filter_cache_size = 128 * 1024 * 1024;

#ifdef HAVE_LIBNFS
    size_t nfs_cache_size = 256 * 1024 * 1024;
#endif

    unsigned translate_cache_size = 131072;
    unsigned translate_stock_limit = 64;

    unsigned tcp_stock_limit = 0;

    unsigned fcgi_stock_limit = 0, fcgi_stock_max_idle = 16;

    unsigned was_stock_limit = 0, was_stock_max_idle = 16;

    unsigned cluster_size = 0, cluster_node = 0;

    bool dynamic_session_cookie = false;

    /**
     * Was #http_cache_size configured explicitly?
     */
    bool http_cache_size_set = false;

    /**
     * Dump widget trees to the log file?
     */
    bool dump_widget_tree = false;

    bool verbose_response = false;

    bool stopwatch = false;

    SpawnConfig spawn;

    void HandleSet(StringView name, const char *value);
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(BpConfig &config, const char *path);

#endif
