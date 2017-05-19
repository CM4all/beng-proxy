/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CONFIG_H
#define BENG_LB_CONFIG_H

#include "lb/ListenerConfig.hxx"
#include "lb/GotoConfig.hxx"
#include "lb/ClusterConfig.hxx"
#include "lb/MonitorConfig.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "certdb/Config.hxx"

#include <map>
#include <list>
#include <string>

struct LbControlConfig {
    AllocatedSocketAddress bind_address;
};

struct LbCertDatabaseConfig : CertDatabaseConfig {
    std::string name;

    /**
     * List of PEM path names containing certificator authorities
     * we're going to use to build the certificate chain.
     */
    std::list<std::string> ca_certs;

    explicit LbCertDatabaseConfig(const char *_name):name(_name) {}
};

struct LbConfig {
    std::string access_logger;

    std::list<LbControlConfig> controls;

    std::map<std::string, LbCertDatabaseConfig> cert_dbs;

    std::map<std::string, LbMonitorConfig> monitors;

    std::map<std::string, LbNodeConfig> nodes;

    std::map<std::string, LbClusterConfig> clusters;
    std::map<std::string, LbBranchConfig> branches;
    std::map<std::string, LbLuaHandlerConfig> lua_handlers;
    std::map<std::string, LbTranslationHandlerConfig> translation_handlers;

    std::list<LbListenerConfig> listeners;

    template<typename T>
    gcc_pure
    const LbMonitorConfig *FindMonitor(T &&t) const {
        const auto i = monitors.find(std::forward<T>(t));
        return i != monitors.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbCertDatabaseConfig *FindCertDb(T &&t) const {
        const auto i = cert_dbs.find(std::forward<T>(t));
        return i != cert_dbs.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbNodeConfig *FindNode(T &&t) const {
        const auto i = nodes.find(std::forward<T>(t));
        return i != nodes.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbClusterConfig *FindCluster(T &&t) const {
        const auto i = clusters.find(std::forward<T>(t));
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbGoto FindGoto(T &&t) const {
        LbGoto g;

        g.cluster = FindCluster(t);
        if (g.cluster == nullptr) {
            g.branch = FindBranch(t);
            if (g.branch == nullptr) {
                g.lua = FindLuaHandler(t);
                if (g.lua == nullptr)
                    g.translation = FindTranslationHandler(t);
            }
        }

        return g;
    }

    template<typename T>
    gcc_pure
    const LbBranchConfig *FindBranch(T &&t) const {
        const auto i = branches.find(std::forward<T>(t));
        return i != branches.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbLuaHandlerConfig *FindLuaHandler(T &&t) const {
        const auto i = lua_handlers.find(std::forward<T>(t));
        return i != lua_handlers.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbTranslationHandlerConfig *FindTranslationHandler(T &&t) const {
        const auto i = translation_handlers.find(std::forward<T>(t));
        return i != translation_handlers.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbListenerConfig *FindListener(T &&t) const {
        for (const auto &i : listeners)
            if (i.name == t)
                return &i;

        return nullptr;
    }

    bool HasCertDatabase() const {
        for (const auto &i : listeners)
            if (i.cert_db != nullptr)
                return true;

        return false;
    }

    gcc_pure
    bool HasZeroConf() const {
        for (const auto &i : listeners)
            if (i.HasZeroConf())
                return true;

        return false;
    }
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(LbConfig &config, const char *path);

#endif
