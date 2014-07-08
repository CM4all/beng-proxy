/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LISTENER_H
#define BENG_PROXY_LB_LISTENER_H

class Error;
class ServerSocket;

struct lb_listener {
    struct lb_instance &instance;

    const struct lb_listener_config &config;

    struct ssl_factory *ssl_factory = nullptr;

    ServerSocket *listener = nullptr;

    lb_listener(struct lb_instance &_instance,
                const struct lb_listener_config &_config)
        :instance(_instance), config(_config) {}
    ~lb_listener();

    bool Setup(Error &error);
};

void
lb_listener_event_add(struct lb_listener *listener);

void
lb_listener_event_del(struct lb_listener *listener);

#endif
