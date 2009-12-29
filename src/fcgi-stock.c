/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-stock.h"
#include "hashmap.h"
#include "child.h"

#include <daemon/log.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

struct fcgi_child {
    pool_t pool;

    struct fcgi_stock *stock;
    const char *executable_path;

    const char *socket_path;

    pid_t pid;
};

struct fcgi_stock {
    pool_t pool;

    struct hashmap *children;
};

struct fcgi_stock *
fcgi_stock_new(pool_t pool)
{
    struct fcgi_stock *stock = p_malloc(pool, sizeof(*stock));

    stock->pool = pool;
    stock->children = hashmap_new(pool, 32);
    return stock;
}

void
fcgi_stock_kill(struct fcgi_stock *stock)
{
    const struct hashmap_pair *pair;

    hashmap_rewind(stock->children);
    while ((pair = hashmap_next(stock->children)) != NULL) {
        struct fcgi_child *child = pair->value;
        kill(child->pid, SIGTERM);
        unlink(child->socket_path);
        child_clear(child->pid);
        pool_unref(child->pool);
    }
}

static void
fcgi_child_callback(int status __attr_unused, void *ctx)
{
    struct fcgi_child *child = ctx;

    hashmap_remove(child->stock->children, child->executable_path);
    pool_unref(child->pool);
}

static const char *
fcgi_child_socket_path(pool_t pool, const char *executable_path __attr_unused)
{
    return p_sprintf(pool, "/tmp/cm4all-beng-proxy-fcgi-%u.socket",
                     (unsigned)random());
}

static int
fcgi_create_socket(const struct fcgi_child *child)
{
    struct sockaddr_un sa;

    size_t socket_path_length = strlen(child->socket_path);
    if (socket_path_length >= sizeof(sa.sun_path)) {
        daemon_log(2, "path too long: %s\n", child->socket_path);
        return -1;
    }

    int ret = unlink(child->socket_path);
    if (ret != 0 && errno != ENOENT) {
        daemon_log(2, "failed to unlink %s: %s\n",
                   child->socket_path, strerror(errno));
        return -1;
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        daemon_log(2, "failed to create unix socket %s: %s\n",
                   child->socket_path, strerror(errno));
        return -1;
    }

    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, child->socket_path, socket_path_length + 1);
    ret = bind(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (ret < 0) {
        daemon_log(2, "bind(%s) failed: %s\n",
                   child->socket_path, strerror(errno));
        close(fd);
        return -1;
    }

    ret = listen(fd, 8);
    if (ret < 0) {
        daemon_log(2, "listen() failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static pid_t
fcgi_spawn_child(const char *executable_path, int fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(2, "fork() failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        dup2(fd, 0);
        close(fd);
        close(1);
        close(2);

        execl(executable_path, executable_path, NULL);
        daemon_log(1, "failed to execute %s: %s\n",
                   executable_path, strerror(errno));
        _exit(1);
    }

    return pid;
}

const char *
fcgi_stock_get(struct fcgi_stock *stock, const char *executable_path)
{
    struct fcgi_child *child = hashmap_get(stock->children, executable_path);
    pool_t pool;
    int fd;

    if (child != NULL)
        return child->socket_path;

    pool = pool_new_libc(stock->pool, "fcgi_child");
    child = p_malloc(pool, sizeof(*child));
    child->pool = pool;
    child->stock = stock;
    child->executable_path = p_strdup(pool, executable_path);
    child->socket_path = fcgi_child_socket_path(pool, executable_path);

    fd = fcgi_create_socket(child);
    if (fd < 0) {
        pool_unref(pool);
        return NULL;
    }

    child->pid = fcgi_spawn_child(executable_path, fd);
    close(fd);
    if (child->pid < 0) {
        pool_unref(pool);
        return NULL;
    }

    hashmap_add(stock->children, child->executable_path, child);
    child_register(child->pid, fcgi_child_callback, child);

    return child->socket_path;
}
