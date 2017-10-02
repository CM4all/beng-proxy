/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_LB_HMONITOR_HXX
#define BENG_PROXY_LB_HMONITOR_HXX

#include "MonitorController.hxx"
#include "util/Compiler.h"

#include <map>
#include <string>

struct pool;
struct LbNodeConfig;
struct LbMonitorConfig;
class EventLoop;
class FailureManager;
class SocketAddress;

/**
 * Map of monitors.
 */
class LbMonitorMap {
    struct Key {
        const char *monitor_name;
        const char *node_name;
        unsigned port;

        gcc_pure
        bool operator<(const Key &other) const;

        std::string ToString() const;
    };

    struct pool *const pool;

    EventLoop &event_loop;
    FailureManager &failure_manager;

    std::map<Key, LbMonitorController> map;

public:
    LbMonitorMap(struct pool &_pool, EventLoop &_event_loop,
                 FailureManager &_failure_manager);
    ~LbMonitorMap();

    void Enable();

    void Add(const char *node_name, SocketAddress address,
             const LbMonitorConfig &config);

    void Add(const LbNodeConfig &node, unsigned port,
             const LbMonitorConfig &config);

    void Clear();
};

#endif
