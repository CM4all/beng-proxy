/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"
#include "http-server.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>

struct instance {
    listener_t listener;
};

static void
my_http_server_callback(struct http_server_request *request,
                        /*const void *body, size_t body_length,*/
                        void *ctx)
{
    (void)request;
    (void)ctx;

    printf("in my_http_server_callback()\n");

    http_server_send_message(request->connection, HTTP_STATUS_OK, "Hello, world!");
}

static void
my_listener_callback(int fd,
                     const struct sockaddr *addr, socklen_t addrlen,
                     void *ctx)
{
    int ret;
    http_server_connection_t connection;

    (void)addr;
    (void)addrlen;
    (void)ctx;

    printf("client %d\n", fd);

    ret = http_server_connection_new(fd, my_http_server_callback, NULL, &connection);
    if (ret < 0)
        close(fd);
}

int main(int argc, char **argv)
{
    int ret;
    struct instance instance;

    (void)argc;
    (void)argv;

    event_init();

    ret = listener_tcp_port_new(8080, &my_listener_callback, NULL, &instance.listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    event_dispatch();

    listener_free(&instance.listener);
}
