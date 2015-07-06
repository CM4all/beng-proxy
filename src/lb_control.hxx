/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONTROL_H
#define BENG_PROXY_LB_CONTROL_H

#include "glibfwd.hxx"

#include <inline/list.h>

struct lb_control_config;
struct ControlServer;

struct LbControl {
    struct list_head siblings;

    struct lb_instance &instance;

    ControlServer *server = nullptr;

    explicit LbControl(struct lb_instance &_instance)
        :instance(_instance) {}

    ~LbControl();

    bool Open(const struct lb_control_config &config, GError **error_r);

    void Enable();
    void Disable();
};

#endif
