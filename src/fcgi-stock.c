/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-stock.h"
#include "fcgi-quark.h"
#include "child.h"
#include "async.h"
#include "client-socket.h"
#include "jail.h"
#include "pevent.h"
#include "exec.h"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <stdlib.h>

struct fcgi_child_params {
    const char *executable_path;

    const struct jail_params *jail;
    const char *document_root;
};

struct fcgi_child {
    struct stock_item base;

    const char *key;

    struct jail_params jail_params;
    const char *document_root;

    struct jail_config jail_config;

    struct sockaddr_un address;

    pid_t pid;

    int fd;
    struct event event;

    struct async_operation create_operation;
    struct async_operation_ref connect_operation;
};

static const char *
fcgi_stock_key(pool_t pool, const struct fcgi_child_params *params)
{
    return params->jail == NULL || !params->jail->enabled
        ? params->executable_path
        : p_strcat(pool, params->executable_path, "|",
                   params->document_root, NULL);
}

static void
fcgi_child_callback(int status __attr_unused, void *ctx)
{
    struct fcgi_child *child = ctx;

    child->pid = -1;
}

static void
fcgi_child_socket_path(struct sockaddr_un *address,
                       const char *executable_path __attr_unused)
{
    address->sun_family = AF_UNIX;

    snprintf(address->sun_path, sizeof(address->sun_path),
             "/tmp/cm4all-beng-proxy-fcgi-%u.socket",
             (unsigned)random());
}

static int
fcgi_create_socket(const struct fcgi_child *child, GError **error_r)
{
    int ret = unlink(child->address.sun_path);
    if (ret != 0 && errno != ENOENT) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "failed to unlink %s: %s",
                    child->address.sun_path, strerror(errno));
        return -1;
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "failed to create unix socket %s: %s",
                    child->address.sun_path, strerror(errno));
        return -1;
    }

    ret = bind(fd, (const struct sockaddr*)&child->address,
               sizeof(child->address));
    if (ret < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "bind(%s) failed: %s",
                    child->address.sun_path, strerror(errno));
        close(fd);
        return -1;
    }

    ret = listen(fd, 8);
    if (ret < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

__attr_noreturn
static void
fcgi_run(const struct jail_params *jail, const char *document_root,
         const char *executable_path,
         int fd)
{
    dup2(fd, 0);
    close(fd);

    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        dup2(fd, 1);
        dup2(fd, 2);
    } else {
        close(1);
        close(2);
    }

    clearenv();

    struct exec e;
    exec_init(&e);
    jail_wrapper_insert(&e, jail, document_root);
    exec_append(&e, executable_path);
    exec_do(&e);

    daemon_log(1, "failed to execute %s: %s\n",
               executable_path, strerror(errno));
    _exit(1);
}

static pid_t
fcgi_spawn_child(const struct jail_params *jail, const char *document_root,
                 const char *executable_path,
                 int fd,
                 GError **error_r)
{
    pid_t pid = fork();
    if (pid < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
        fcgi_run(jail, document_root,
                 executable_path,
                 fd);

    return pid;
}

/*
 * libevent callback
 *
 */

static void
fcgi_child_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct fcgi_child *child = ctx;

    assert(fd == child->fd);

    p_event_consumed(&child->event, child->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle FastCGI connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle FastCGI connection\n");
    }

    stock_del(&child->base);
    pool_commit();
}

/*
 * client_socket callback
 *
 */

static void
fcgi_connect_callback(int fd, int err, void *ctx)
{
    struct fcgi_child *child = ctx;

    unlink(child->address.sun_path);

    async_ref_clear(&child->connect_operation);

    if (err == 0) {
        assert(fd >= 0);

        child->fd = fd;

        event_set(&child->event, child->fd, EV_READ|EV_TIMEOUT,
                  fcgi_child_event_callback, child);

        stock_item_available(&child->base);
    } else {
        GError *error =
            g_error_new(g_file_error_quark(), err,
                        "failed to connect to FastCGI server '%s': %s",
                        child->key, strerror(err));

        stock_item_failed(&child->base, error);
    }
}

/*
 * async operation
 *
 */

static struct fcgi_child *
async_to_fcgi_child(struct async_operation *ao)
{
    return (struct fcgi_child*)(((char*)ao) - offsetof(struct fcgi_child, create_operation));
}

static void
fcgi_create_abort(struct async_operation *ao)
{
    struct fcgi_child *child = async_to_fcgi_child(ao);

    assert(child != NULL);
    assert(async_ref_defined(&child->connect_operation));

    unlink(child->address.sun_path);

    if (child->pid >= 0) {
        kill(child->pid, SIGTERM);
        child_clear(child->pid);
    }

    async_abort(&child->connect_operation);
    stock_item_aborted(&child->base);
}

static const struct async_operation_class fcgi_create_operation = {
    .abort = fcgi_create_abort,
};

/*
 * stock class
 *
 */

static pool_t
fcgi_stock_pool(void *ctx __attr_unused, pool_t parent,
               const char *uri __attr_unused)
{
    return pool_new_linear(parent, "fcgi_child", 2048);
}

static void
fcgi_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                  const char *key, void *info,
                  pool_t caller_pool,
                  struct async_operation_ref *async_ref)
{
    pool_t pool = item->pool;
    struct fcgi_child_params *params = info;
    struct fcgi_child *child = (struct fcgi_child *)item;

    assert(key != NULL);
    assert(params != NULL);
    assert(params->executable_path != NULL);

    child->key = p_strdup(pool, key);
    fcgi_child_socket_path(&child->address, key);

    if (params->jail != NULL && params->jail->enabled) {
        jail_params_copy(pool, &child->jail_params, params->jail);
        child->document_root = p_strdup_checked(pool, params->document_root);

        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    } else
        child->jail_params.enabled = false;

    GError *error = NULL;
    int fd = fcgi_create_socket(child, &error);
    if (fd < 0) {
        stock_item_failed(item, error);
        return;
    }

    child->pid = fcgi_spawn_child(params->jail, params->document_root,
                                  params->executable_path,
                                  fd, &error);
    close(fd);
    if (child->pid < 0) {
        stock_item_failed(item, error);
        return;
    }

    child_register(child->pid, fcgi_child_callback, child);

    child->fd = -1;

    async_init(&child->create_operation, &fcgi_create_operation);
    async_ref_set(async_ref, &child->create_operation);

    client_socket_new(caller_pool, AF_UNIX, SOCK_STREAM, 0,
                      (const struct sockaddr*)&child->address,
                      sizeof(child->address),
                      fcgi_connect_callback, child,
                      &child->connect_operation);
}

static bool
fcgi_stock_borrow(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
fcgi_stock_release(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;
    struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "fcgi_child_event");
}

static void
fcgi_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child =
        (struct fcgi_child *)item;

    if (child->pid >= 0) {
        kill(child->pid, SIGTERM);
        child_clear(child->pid);
    }

    if (async_ref_defined(&child->connect_operation))
        async_abort(&child->connect_operation);
    else if (child->fd >= 0) {
        p_event_del(&child->event, child->base.pool);
        close(child->fd);
    }
}

static const struct stock_class fcgi_stock_class = {
    .item_size = sizeof(struct fcgi_child),
    .pool = fcgi_stock_pool,
    .create = fcgi_stock_create,
    .borrow = fcgi_stock_borrow,
    .release = fcgi_stock_release,
    .destroy = fcgi_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
fcgi_stock_new(pool_t pool, unsigned limit)
{
    return hstock_new(pool, &fcgi_stock_class, NULL, limit);
}

void
fcgi_stock_get(struct hstock *hstock, pool_t pool,
               const struct jail_params *jail,
               const char *executable_path, const char *document_root,
               const struct stock_handler *handler, void *handler_ctx,
               struct async_operation_ref *async_ref)
{
    if (jail != NULL && jail->enabled && jail->home_directory == NULL) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "No home directory for jailed FastCGI");
        handler->error(error, handler_ctx);
        return;
    }

    struct fcgi_child_params *params = p_malloc(pool, sizeof(*params));
    params->executable_path = executable_path;
    params->jail = jail;
    params->document_root = document_root;

    hstock_get(hstock, pool, fcgi_stock_key(pool, params), params,
               handler, handler_ctx, async_ref);
}

int
fcgi_stock_item_get_domain(const struct stock_item *item)
{
    (void)item;

    return AF_UNIX;
}

int
fcgi_stock_item_get(const struct stock_item *item)
{
    const struct fcgi_child *child = (const struct fcgi_child *)item;

    assert(child->fd >= 0);

    return child->fd;
}

const char *
fcgi_stock_translate_path(const struct stock_item *item,
                          const char *path, pool_t pool)
{
    const struct fcgi_child *child = (const struct fcgi_child *)item;

    if (!child->jail_params.enabled)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = jail_translate_path(&child->jail_config, path,
                                             child->jail_params.home_directory,
                                             pool);
    return jailed != NULL ? jailed : path;
}

void
fcgi_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy)
{
    struct fcgi_child *child = (struct fcgi_child *)item;

    hstock_put(hstock, child->key, item, destroy);
}
