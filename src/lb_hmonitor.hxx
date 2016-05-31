/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HMONITOR_HXX
#define BENG_PROXY_LB_HMONITOR_HXX

#include <inline/compiler.h>

#include <map>

struct pool;
struct LbNodeConfig;
struct LbMonitorConfig;
struct LbMonitor;
class EventLoop;

class LbMonitorMap {
    struct Key {
        const char *monitor_name;
        const char *node_name;
        unsigned port;

        gcc_pure
        bool operator<(const Key &other) const;

        char *ToString(struct pool &pool) const;
    };

    struct pool *const pool;

    std::map<Key, LbMonitor *> map;

public:
    LbMonitorMap(struct pool &_pool);
    ~LbMonitorMap();

    void Enable();

    void Add(const LbNodeConfig &node, unsigned port,
             const LbMonitorConfig &config, EventLoop &event_loop);

    void Clear();
};

#endif
